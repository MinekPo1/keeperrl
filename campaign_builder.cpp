#include "stdafx.h"
#include "campaign_builder.h"
#include "options.h"
#include "campaign_type.h"
#include "util.h"
#include "view.h"
#include "enemy_factory.h"
#include "villain_type.h"
#include "creature.h"
#include "creature_name.h"
#include "retired_games.h"
#include "view_object.h"
#include "name_generator.h"
#include "creature_factory.h"
#include "tribe_alignment.h"
#include "external_enemies_type.h"
#include "enemy_aggression_level.h"
#include "campaign_info.h"
#include "content_factory.h"
#include "avatar_info.h"
#include "layout_canvas.h"
#include "layout_generator.h"
#include "enemy_info.h"
#include "villain_group.h"

void CampaignBuilder::setCountLimits(const CampaignInfo& info) {
  int minMainVillains =
#ifdef RELEASE
  1;
#else
  0;
#endif
  options->setLimits(OptionId::MAIN_VILLAINS, Range(minMainVillains, info.maxMainVillains + 1));
  options->setLimits(OptionId::LESSER_VILLAINS, Range(0, info.maxLesserVillains + 1));
  options->setLimits(OptionId::MINOR_VILLAINS, Range(0, info.maxMinorVillains + 1));
  options->setLimits(OptionId::ALLIES, Range(0, info.maxAllies + 1));
}

vector<OptionId> CampaignBuilder::getCampaignOptions(CampaignType type) const {
  switch (type) {
    case CampaignType::QUICK_MAP:
      return {OptionId::LESSER_VILLAINS, OptionId::ALLIES};
    case CampaignType::FREE_PLAY:
      return {
        OptionId::MAIN_VILLAINS,
        OptionId::LESSER_VILLAINS,
        OptionId::MINOR_VILLAINS,
        OptionId::ALLIES,
        OptionId::ENDLESS_ENEMIES,
        OptionId::ENEMY_AGGRESSION,
      };
  }
}

vector<CampaignType> CampaignBuilder::getAvailableTypes() const {
  return {
    CampaignType::FREE_PLAY,
#ifndef RELEASE
    CampaignType::QUICK_MAP,
#endif
  };
}

const char* CampaignBuilder::getIntroText() const {
  return
    "Welcome to the campaign mode! "
    "The world, which you see below, is made up of smaller maps. You will build your base on one of them. "
    "There are hostile and friendly tribes around you. You have to conquer all villains marked as \"main\" "
    "to win the game."
    "You can travel to other sites by creating a team and using the travel command.\n\n"
    "The highlighted tribes are in your influence zone, which means that you can currently interact with them "
    "(trade, recruit, attack or be attacked). "
    "As you conquer more enemies, your influence zone will increase.\n\n";
}

void CampaignBuilder::setPlayerPos(Campaign& campaign, Vec2 pos, ViewIdList playerViewId, ContentFactory* f) {
  campaign.sites[campaign.playerPos].dweller.reset();
  campaign.playerPos = pos;
  campaign.sites[campaign.playerPos].dweller =
      Campaign::SiteInfo::Dweller(Campaign::KeeperInfo{playerViewId,
          avatarInfo.playerCreature->getTribeId()});
  campaign.updateInhabitants(f);
}

static bool tileExists(const ContentFactory* factory, const string& s) {
  for (auto& def : factory->tilePaths.definitions)
    if (s == def.viewId.data())
      return true;
  return false;
}

static Table<Campaign::SiteInfo> getTerrain(RandomGen& random, const ContentFactory* factory,
    RandomLayoutId worldMapId, Vec2 size) {
  LayoutCanvas::Map map{Table<vector<Token>>(Rectangle(Vec2(0, 0), size))};
  LayoutCanvas canvas{map.elems.getBounds(), &map};
  bool generated = false;
  for (int i : Range(20))
    if (factory->randomLayouts.at(worldMapId).make(canvas, random)) {
      generated = true;
      break;
    }
  CHECK(generated) << "Failed to generate world map";
  Table<Campaign::SiteInfo> ret(size, {});
  for (Vec2 v : ret.getBounds())
    for (auto& token : map.elems[v]) {
      if (token == "blocked")
        ret[v].blocked = true;
      else if (tileExists(factory, token))
        ret[v].viewId.push_back(ViewId(token.data()));
      else
        for (auto& biome : factory->biomeInfo)
          if (token == biome.first.data())
            ret[v].biome = biome.first;
    }
  return ret;
}

struct VillainCounts {
  int numMain;
  int numLesser;
  int numMinor;
  int numAllies;
  int maxRetired;
};

static VillainCounts getVillainCounts(CampaignType type, Options* options) {
  switch (type) {
    case CampaignType::FREE_PLAY: {
      return {
        options->getIntValue(OptionId::MAIN_VILLAINS),
        options->getIntValue(OptionId::LESSER_VILLAINS),
        options->getIntValue(OptionId::MINOR_VILLAINS),
        options->getIntValue(OptionId::ALLIES),
        10000
      };
    }
    case CampaignType::QUICK_MAP:
      return {0, 0, 0, 0};
  }
}

CampaignBuilder::CampaignBuilder(View* v, RandomGen& rand, Options* o, VillainsTuple villains, GameIntros intros, const AvatarInfo& a)
    : view(v), random(rand), options(o), villains(std::move(villains)), gameIntros(intros), avatarInfo(a) {
}

static string getNewIdSuffix() {
  vector<char> chars;
  for (char c : Range(128))
    if (isalnum(c))
      chars.push_back(c);
  string ret;
  for (int i : Range(4))
    ret += Random.choose(chars);
  return ret;
}

bool CampaignBuilder::placeVillains(const ContentFactory* contentFactory, Campaign& campaign,
    vector<Campaign::SiteInfo::Dweller> villains, int count) {
  for (int i = 0; villains.size() < count; ++i)
    villains.push_back(villains[i]);
  if (villains.size() > count)
    villains.resize(count);
  auto isFreeSpot = [&](Vec2 v) {
    if (campaign.sites[v].blocked || !v.inRectangle(campaign.sites.getBounds().minusMargin(10)))
      return false;
    for (auto v2 : Rectangle::centered(v, 5))
      if (v2.inRectangle(campaign.sites.getBounds()) && !campaign.sites[v2].isEmpty())
        return false;
    return true;
  };
  for (int i : All(villains)) {
    auto biome = [&] {
      return villains[i].match(
          [&](const Campaign::VillainInfo& info) { return contentFactory->enemies.at(info.enemyId).getBiome(); },
          [](const Campaign::RetiredInfo& info) -> optional<BiomeId> { return none; },
          [](const Campaign::KeeperInfo& info) -> optional<BiomeId> { return none; }
      );
    }();
    auto placed = [&] {
      for (Vec2 v : random.permutation(campaign.sites.getBounds().getAllSquares())) {
        if (isFreeSpot(v) && (!biome || campaign.sites[v].biome == biome)) {
          campaign.sites[v].dweller = villains[i];
          return true;
        }
      }
      return false;
    }();
    if (!placed)
      return false;
/*      USER_FATAL << "Couldn't place " << villains[i].match(
              [&](const Campaign::VillainInfo& info) { return info.enemyId.data(); },
              [](const Campaign::RetiredInfo& info) { return info.gameInfo.name.data(); },
              [](const Campaign::KeeperInfo& info) { return "Home map"; })
          << " in " << (biome ? biome->data() : "Any biome");*/
  }
  return true;
}

using Dweller = Campaign::SiteInfo::Dweller;

vector<Dweller> shuffle(RandomGen& random, vector<Campaign::VillainInfo> v) {
  int numAlways = 0;
  for (auto& elem : v)
    if (elem.alwaysPresent) {
      swap(v[numAlways], elem);
      ++numAlways;
    }
  random.shuffle(v.begin() + numAlways, v.end());
  return v.transform([](auto& t) { return Dweller(t); });
}

vector<Campaign::VillainInfo> CampaignBuilder::getVillains(const vector<VillainGroup>& groups, VillainType type) {
  vector<Campaign::VillainInfo> ret;
  for (auto& group : groups)
    for (auto& elem : villains[group])
      if (elem.type == type)
        ret.push_back(elem);
  return ret;
}

bool CampaignBuilder::placeVillains(const ContentFactory* contentFactory, Campaign& campaign,
    const VillainCounts& counts, const optional<RetiredGames>& retired, const vector<VillainGroup>& villainGroups) {
  int retiredLimit = counts.numMain;
  auto mainVillains = getVillains(villainGroups, VillainType::MAIN);
  for (auto& v : mainVillains)
    if (v.alwaysPresent && retiredLimit > 0)
      --retiredLimit;
  int numRetired = retired ? min(retired->getNumActive(), min(retiredLimit, counts.maxRetired)) : 0;
  if (!placeVillains(contentFactory, campaign, shuffle(random, mainVillains), counts.numMain - numRetired) ||
      !placeVillains(contentFactory, campaign, shuffle(random, getVillains(villainGroups, VillainType::LESSER)),
          counts.numLesser) ||
      !placeVillains(contentFactory, campaign, shuffle(random, getVillains(villainGroups, VillainType::MINOR)),
          counts.numMinor) ||
      !placeVillains(contentFactory, campaign, shuffle(random, getVillains(villainGroups, VillainType::ALLY)),
          counts.numAllies))
    return false;
  if (retired && !placeVillains(contentFactory, campaign, retired->getActiveGames().transform(
      [](const RetiredGames::RetiredGame& game) -> Dweller {
        return Campaign::RetiredInfo{game.gameInfo, game.fileInfo};
      }), numRetired))
    return false;
  return true;
}

static bool autoConfirm(CampaignType type) {
  switch (type) {
    case CampaignType::QUICK_MAP:
      return true;
    default:
      return false;
  }
}

const vector<string>& CampaignBuilder::getIntroMessages(CampaignType type) const {
  return gameIntros;
}

static optional<ExternalEnemiesType> getExternalEnemies(Options* options) {
  auto v = options->getIntValue(OptionId::ENDLESS_ENEMIES);
  if (v == 0)
    return none;
  if (v == 1)
    return ExternalEnemiesType::FROM_START;
  if (v == 2)
    return ExternalEnemiesType::AFTER_WINNING;
  FATAL << "Bad endless enemies value " << v;
  fail();
}

static EnemyAggressionLevel getAggressionLevel(Options* options) {
  auto v = options->getIntValue(OptionId::ENEMY_AGGRESSION);
  if (v == 0)
    return EnemyAggressionLevel::NONE;
  if (v == 1)
    return EnemyAggressionLevel::MODERATE;
  if (v == 2)
    return EnemyAggressionLevel::EXTREME;
  FATAL << "Bad enemy aggression value " << v;
  fail();
}

static bool isGoodStartingPos(const Campaign& campaign, Vec2 pos, int visibilityRadius, int totalLesserVillains) {
  if (!campaign.isGoodStartPos(pos))
    return false;
  int numLesser = 0;
  for (auto v : campaign.getSites().getBounds().intersection(Rectangle::centered(pos, visibilityRadius)))
    if (campaign.getSites()[v].getVillainType() == VillainType::LESSER && v.distD(pos) <= visibilityRadius + 0.5)
      ++numLesser;
  return numLesser >= min(3, totalLesserVillains);
}

optional<CampaignSetup> CampaignBuilder::prepareCampaign(ContentFactory* contentFactory,
    function<optional<RetiredGames>(CampaignType)> genRetired,
    CampaignType type, string worldName) {
  auto& campaignInfo = contentFactory->campaignInfo;
  Vec2 size = campaignInfo.size;
  int numBlocked = 0.6 * size.x * size.y;
  auto retired = genRetired(type);
  View::CampaignMenuState menuState { true, CampaignMenuIndex{CampaignMenuElems::None{}} };
  options->setChoices(OptionId::ENDLESS_ENEMIES, {"none", "from the start", "after winning"});
  options->setChoices(OptionId::ENEMY_AGGRESSION, {"none", "moderate", "extreme"});
  int worldMapIndex = 0;
  auto worldMapId = [&] {
    return contentFactory->worldMaps[worldMapIndex].layout;
  };
  int failedPlaceVillains = 0;
  while (1) {
    setCountLimits(campaignInfo);
    Table<Campaign::SiteInfo> terrain = getTerrain(random, contentFactory, worldMapId(), size);
    Campaign campaign(terrain, type, worldName);
    campaign.mapZoom = campaignInfo.mapZoom;
    campaign.minimapZoom = campaignInfo.minimapZoom;
    const auto villainCounts = getVillainCounts(type, options);
    if (!placeVillains(contentFactory, campaign, villainCounts, retired, avatarInfo.villainGroups)) {
      if (++failedPlaceVillains > 300)
        USER_FATAL << "Failed to place all villains on the world map";
      continue;
    }
    for (auto pos : Random.permutation(campaign.getSites().getBounds()
        .minusMargin(campaignInfo.initialRadius + 1).getAllSquares())) {
      if (isGoodStartingPos(campaign, pos, campaignInfo.initialRadius, villainCounts.numLesser) ||
          type == CampaignType::QUICK_MAP) {
        setPlayerPos(campaign, pos, avatarInfo.playerCreature->getMaxViewIdUpgrade(), contentFactory);
        campaign.originalPlayerPos = pos;
        break;
      }
    }
    while (1) {
      bool updateMap = false;
      campaign.refreshInfluencePos(contentFactory);
      CampaignAction action = autoConfirm(type) ? CampaignActionId::CONFIRM
          : view->prepareCampaign(View::CampaignOptions {
              campaign,
              (retired && type == CampaignType::FREE_PLAY) ? optional<RetiredGames&>(*retired) : none,
              getCampaignOptions(type),
              getIntroText(),
              contentFactory->biomeInfo.at(*campaign.getSites()[campaign.getPlayerPos()].biome).name,
              contentFactory->worldMaps.transform([](auto& elem) { return elem.name; }),
              worldMapIndex
            },
              menuState);
      switch (action.getId()) {
        case CampaignActionId::REROLL_MAP:
          terrain = getTerrain(random, contentFactory, worldMapId(), size);
          updateMap = true;
          break;
        case CampaignActionId::UPDATE_MAP:
          updateMap = true;
          break;
        case CampaignActionId::SET_POSITION:
          setPlayerPos(campaign, action.get<Vec2>(), avatarInfo.playerCreature->getMaxViewIdUpgrade(),
              contentFactory);
          break;
        case CampaignActionId::CHANGE_WORLD_MAP:
          worldMapIndex = action.get<int>();
          retired = genRetired(type);
          updateMap = true;
          break;
        case CampaignActionId::UPDATE_OPTION:
          switch (action.get<OptionId>()) {
            case OptionId::PLAYER_NAME:
            case OptionId::ENDLESS_ENEMIES:
            case OptionId::ENEMY_AGGRESSION:
              break;
            default:
              updateMap = true;
              break;
          }
          break;
        case CampaignActionId::CANCEL:
          return none;
        case CampaignActionId::CONFIRM:
          if (!retired || retired->getNumActive() > 0 || retired->getAllGames().empty() ||
              view->yesOrNoPrompt("The imps are going to be sad if you don't add any retired dungeons. Continue?")) {
            string gameIdentifier;
            string gameDisplayName;
            if (avatarInfo.chosenBaseName) {
              string name = avatarInfo.playerCreature->getName().firstOrBare();
              gameIdentifier = *avatarInfo.chosenBaseName + "_" + campaign.worldName + getNewIdSuffix();
              gameDisplayName = capitalFirst(avatarInfo.playerCreature->getName().plural()) + " of " + *avatarInfo.chosenBaseName;
            } else {
              string name = avatarInfo.playerCreature->getName().firstOrBare();
              gameIdentifier = name + "_" + campaign.worldName + getNewIdSuffix();
              gameDisplayName = name + " of " + campaign.worldName;
            }
            gameIdentifier = stripFilename(std::move(gameIdentifier));
            auto aggressionLevel = avatarInfo.creatureInfo.enemyAggression
                ? getAggressionLevel(options)
                : EnemyAggressionLevel::NONE;
            return CampaignSetup{campaign, gameIdentifier, gameDisplayName,
                getIntroMessages(type), getExternalEnemies(options), aggressionLevel};
          }
      }
      if (updateMap)
        break;
    }
  }
}

CampaignSetup CampaignBuilder::getEmptyCampaign() {
  Campaign ret(Table<Campaign::SiteInfo>(1, 1), CampaignType::QUICK_MAP, "");
  return CampaignSetup{ret, "", "", {}, none, EnemyAggressionLevel::MODERATE};
}

CampaignSetup CampaignBuilder::getWarlordCampaign(const vector<RetiredGames::RetiredGame>& games,
    const string& gameName) {
  Campaign ret(Table<Campaign::SiteInfo>(games.size(), 1), CampaignType::QUICK_MAP, "");
  for (int i : All(games)) {
    auto site = Campaign::SiteInfo {
      games[i].gameInfo.getViewId(),
      {},
      Campaign::SiteInfo::Dweller(Campaign::RetiredInfo { games[i].gameInfo, games[i].fileInfo }),
      false
    };
    ret.sites[i][0] = std::move(site);
  }
  ret.mapZoom = 2;
  ret.playerPos = Vec2(0, 0);
  return CampaignSetup{std::move(ret), stripFilename(gameName + getNewIdSuffix()), gameName, {}, none,
      EnemyAggressionLevel::MODERATE};
}
