#include "stdafx.h"
#include "model_builder.h"
#include "level.h"
#include "tribe.h"
#include "inventory.h"
#include "collective_builder.h"
#include "options.h"
#include "player_control.h"
#include "spectator.h"
#include "creature.h"
#include "square.h"
#include "progress_meter.h"
#include "collective.h"
#include "level_maker.h"
#include "model.h"
#include "level_builder.h"
#include "monster_ai.h"
#include "game.h"
#include "campaign.h"
#include "creature_name.h"
#include "villain_type.h"
#include "enemy_factory.h"
#include "view_object.h"
#include "item.h"
#include "furniture.h"
#include "sokoban_input.h"
#include "external_enemies.h"
#include "immigration.h"
#include "technology.h"
#include "keybinding.h"
#include "tutorial.h"
#include "settlement_info.h"
#include "enemy_info.h"
#include "game_time.h"
#include "lasting_effect.h"
#include "skill.h"
#include "game_config.h"
#include "build_info.h"

using namespace std::chrono;

ModelBuilder::ModelBuilder(ProgressMeter* m, RandomGen& r, Options* o, SokobanInput* sok, GameConfig* gameConfig)
    : random(r), meter(m), options(o), enemyFactory(EnemyFactory(random)), sokobanInput(sok), gameConfig(gameConfig) {
}

ModelBuilder::~ModelBuilder() {
}

static CollectiveConfig getKeeperConfig(RandomGen& random, bool fastImmigration, bool regenerateMana) {
  return CollectiveConfig::keeper(
      TimeInterval(fastImmigration ? 10 : 140),
      10,
      regenerateMana);
}

SettlementInfo& ModelBuilder::makeExtraLevel(WModel model, EnemyInfo& enemy) {
  const int towerHeight = random.get(7, 12);
  const int gnomeHeight = random.get(3, 5);
  SettlementInfo& mainSettlement = enemy.settlement;
  SettlementInfo& extraSettlement = enemy.levelConnection->otherEnemy->settlement;
  switch (enemy.levelConnection->type) {
    case LevelConnection::TOWER: {
      StairKey downLink = StairKey::getNew();
      extraSettlement.upStairs = {downLink};
      for (int i : Range(towerHeight - 1)) {
        StairKey upLink = StairKey::getNew();
        model->buildLevel(
            LevelBuilder(meter, random, 4, 4, "Tower floor" + toString(i + 2)),
            LevelMaker::towerLevel(random,
                CONSTRUCT(SettlementInfo,
                  c.type = SettlementType::TOWER;
                  c.inhabitants.fighters = CreatureList(
                      random.get(1, 3),
                      random.choose(
                          CreatureId::WATER_ELEMENTAL, CreatureId::AIR_ELEMENTAL, CreatureId::FIRE_ELEMENTAL,
                          CreatureId::EARTH_ELEMENTAL));
                  c.tribe = enemy.settlement.tribe;
                  c.collective = new CollectiveBuilder(CollectiveConfig::noImmigrants(), c.tribe);
                  c.upStairs = {upLink};
                  c.downStairs = {downLink};
                  c.furniture = FurnitureFactory(TribeId::getHuman(), FurnitureType::GROUND_TORCH);
                  if (enemy.levelConnection->deadInhabitants) {
                    c.corpses = c.inhabitants;
                    c.inhabitants = InhabitantsInfo{};
                  }
                  c.buildingId = BuildingId::BRICK;)));
        downLink = upLink;
      }
      mainSettlement.downStairs = {downLink};
      model->buildLevel(
         LevelBuilder(meter, random, 5, 5, "Tower top"),
         LevelMaker::towerLevel(random, mainSettlement));
      return extraSettlement;
    }
    case LevelConnection::CRYPT: {
      StairKey key = StairKey::getNew();
      extraSettlement.downStairs = {key};
      mainSettlement.upStairs = {key};
      model->buildLevel(
         LevelBuilder(meter, random, 40, 40, "Crypt"),
         LevelMaker::cryptLevel(random, mainSettlement));
      return extraSettlement;
    }
    case LevelConnection::MAZE: {
      StairKey key = StairKey::getNew();
      extraSettlement.upStairs = {key};
      mainSettlement.downStairs = {key};
      model->buildLevel(
         LevelBuilder(meter, random, 40, 40, "Maze"),
         LevelMaker::mazeLevel(random, extraSettlement));
      return mainSettlement;
    }
    case LevelConnection::GNOMISH_MINES: {
      StairKey upLink = StairKey::getNew();
      extraSettlement.downStairs = {upLink};
      for (int i : Range(gnomeHeight - 1)) {
        StairKey downLink = StairKey::getNew();
        model->buildLevel(
            LevelBuilder(meter, random, 60, 40, "Mines lvl " + toString(i + 1)),
            LevelMaker::roomLevel(random, CreatureFactory::gnomishMines(
                mainSettlement.tribe, TribeId::getMonster(), 0),
                CreatureFactory::waterCreatures(TribeId::getMonster()),
                CreatureFactory::lavaCreatures(TribeId::getMonster()), {upLink}, {downLink},
                FurnitureFactory::roomFurniture(TribeId::getPest())));
        upLink = downLink;
      }
      mainSettlement.upStairs = {upLink};
      model->buildLevel(
         LevelBuilder(meter, random, 60, 40, "Mine Town"),
         LevelMaker::mineTownLevel(random, mainSettlement));
      return extraSettlement;
    }
    case LevelConnection::SOKOBAN:
      StairKey key = StairKey::getNew();
      extraSettlement.upStairs = {key};
      mainSettlement.downStairs = {key};
      Table<char> sokoLevel = sokobanInput->getNext();
      model->buildLevel(
          LevelBuilder(meter, random, sokoLevel.getBounds().width(), sokoLevel.getBounds().height(), "Sokoban"),
          LevelMaker::sokobanFromFile(random, mainSettlement, sokoLevel));
      return extraSettlement;
  }
}

PModel ModelBuilder::singleMapModel(const string& worldName, TribeId keeperTribe) {
  return tryBuilding(10, [&] { return trySingleMapModel(worldName, keeperTribe);}, "single map");
}

PModel ModelBuilder::trySingleMapModel(const string& worldName, TribeId keeperTribe) {
  vector<EnemyInfo> enemies;
  for (int i : Range(random.get(3, 6)))
    enemies.push_back(enemyFactory->get(EnemyId::HUMAN_COTTAGE));
  enemies.push_back(enemyFactory->get(EnemyId::KOBOLD_CAVE));
  for (int i : Range(random.get(2, 4)))
    enemies.push_back(enemyFactory->get(random.choose({EnemyId::BANDITS, EnemyId::COTTAGE_BANDITS}, {3, 1}))
        .setVillainType(VillainType::LESSER));
  enemies.push_back(enemyFactory->get(random.choose(EnemyId::GNOMES, EnemyId::DARK_ELVES)).setVillainType(VillainType::ALLY));
  append(enemies, enemyFactory->getVaults());
  enemies.push_back(enemyFactory->get(EnemyId::ANTS_CLOSED).setVillainType(VillainType::LESSER));
  enemies.push_back(enemyFactory->get(EnemyId::DWARVES).setVillainType(VillainType::MAIN));
  enemies.push_back(enemyFactory->get(EnemyId::KNIGHTS).setVillainType(VillainType::MAIN));
  enemies.push_back(enemyFactory->get(EnemyId::ADA_GOLEMS));
  enemies.push_back(enemyFactory->get(random.choose(EnemyId::OGRE_CAVE, EnemyId::HARPY_CAVE))
      .setVillainType(VillainType::ALLY));
  for (auto& enemy : random.chooseN(2, {
        EnemyId::ELEMENTALIST,
        EnemyId::WARRIORS,
        EnemyId::ELVES,
        EnemyId::VILLAGE}))
    enemies.push_back(enemyFactory->get(enemy).setVillainType(VillainType::MAIN));
  for (auto& enemy : random.chooseN(2, {
        EnemyId::GREEN_DRAGON,
        EnemyId::SHELOB,
        EnemyId::HYDRA,
        EnemyId::RED_DRAGON,
        EnemyId::CYCLOPS,
        EnemyId::DRIADS,
        EnemyId::ENTS}))
    enemies.push_back(enemyFactory->get(enemy).setVillainType(VillainType::LESSER));
  for (auto& enemy : random.chooseN(1, {
        EnemyId::KRAKEN,
        EnemyId::WITCH,
        EnemyId::CEMETERY}))
    enemies.push_back(enemyFactory->get(enemy));
  return tryModel(304, worldName, enemies, keeperTribe, BiomeId::GRASSLAND, {}, true);
}

void ModelBuilder::addMapVillains(vector<EnemyInfo>& enemyInfo, BiomeId biomeId) {
  switch (biomeId) {
    case BiomeId::GRASSLAND:
      for (int i : Range(random.get(3, 5)))
        enemyInfo.push_back(enemyFactory->get(EnemyId::HUMAN_COTTAGE));
      if (random.roll(2))
        enemyInfo.push_back(enemyFactory->get(EnemyId::COTTAGE_BANDITS));
      break;
    case BiomeId::MOUNTAIN:
      for (int i : Range(random.get(1, 4)))
        enemyInfo.push_back(enemyFactory->get(random.choose(EnemyId::DWARF_CAVE, EnemyId::KOBOLD_CAVE)));
      for (int i : Range(random.get(0, 3)))
        enemyInfo.push_back(enemyFactory->get(random.choose({EnemyId::BANDITS, EnemyId::NO_AGGRO_BANDITS}, {1, 4})));
      break;
    case BiomeId::FORREST:
      for (int i : Range(random.get(3, 5)))
        enemyInfo.push_back(enemyFactory->get(EnemyId::ELVEN_COTTAGE));
      break;
  }
}

PModel ModelBuilder::tryCampaignBaseModel(const string& siteName, TribeId keeperTribe, bool addExternalEnemies) {
  vector<EnemyInfo> enemyInfo;
  BiomeId biome = BiomeId::MOUNTAIN;
  enemyInfo.push_back(enemyFactory->get(EnemyId::DWARF_CAVE));
  enemyInfo.push_back(enemyFactory->get(EnemyId::BANDITS));
  enemyInfo.push_back(enemyFactory->get(EnemyId::ANTS_CLOSED_SMALL));
  enemyInfo.push_back(enemyFactory->get(EnemyId::ADA_GOLEMS));
  enemyInfo.push_back(enemyFactory->get(EnemyId::TUTORIAL_VILLAGE));
  append(enemyInfo, enemyFactory->getVaults());
  if (random.chance(0.3))
    enemyInfo.push_back(enemyFactory->get(EnemyId::KRAKEN));
  optional<ExternalEnemies> externalEnemies;
  if (addExternalEnemies)
    externalEnemies = ExternalEnemies(random, enemyFactory->getExternalEnemies());
  return tryModel(174, siteName, enemyInfo, keeperTribe, biome, std::move(externalEnemies), true);
}

PModel ModelBuilder::tryTutorialModel(const string& siteName) {
  vector<EnemyInfo> enemyInfo;
  BiomeId biome = BiomeId::MOUNTAIN;
  /*enemyInfo.push_back(enemyFactory->get(EnemyId::BANDITS));
  enemyInfo.push_back(enemyFactory->get(EnemyId::ADA_GOLEMS));*/
  //enemyInfo.push_back(enemyFactory->get(EnemyId::KRAKEN));
  enemyInfo.push_back(enemyFactory->get(EnemyId::TUTORIAL_VILLAGE));
  return tryModel(174, siteName, enemyInfo, TribeId::getDarkKeeper(), biome, {}, false);
}

static optional<BiomeId> getBiome(EnemyInfo& enemy, RandomGen& random) {
  switch (enemy.settlement.type) {
    case SettlementType::CASTLE:
    case SettlementType::CASTLE2:
    case SettlementType::TOWER:
    case SettlementType::VILLAGE:
    case SettlementType::SWAMP:
      return BiomeId::GRASSLAND;
    case SettlementType::CAVE:
    case SettlementType::MINETOWN:
    case SettlementType::SMALL_MINETOWN:
    case SettlementType::ANT_NEST:
      return BiomeId::MOUNTAIN;
    case SettlementType::FORREST_COTTAGE:
    case SettlementType::FORREST_VILLAGE:
    case SettlementType::ISLAND_VAULT_DOOR:
    case SettlementType::FOREST:
      return BiomeId::FORREST;
    case SettlementType::CEMETERY:
      return random.choose(BiomeId::GRASSLAND, BiomeId::FORREST);
    default: return none;
  }
}

PModel ModelBuilder::tryCampaignSiteModel(const string& siteName, EnemyId enemyId, VillainType type) {
  vector<EnemyInfo> enemyInfo { enemyFactory->get(enemyId).setVillainType(type)};
  auto biomeId = getBiome(enemyInfo[0], random);
  CHECK(biomeId) << "Unimplemented enemy in campaign " << EnumInfo<EnemyId>::getString(enemyId);
  addMapVillains(enemyInfo, *biomeId);
  return tryModel(114, siteName, enemyInfo, none, *biomeId, {}, true);
}

PModel ModelBuilder::tryBuilding(int numTries, function<PModel()> buildFun, const string& name) {
  for (int i : Range(numTries)) {
    try {
      if (meter)
        meter->reset();
      return buildFun();
    } catch (LevelGenException) {
      INFO << "Retrying level gen";
    }
  }
  FATAL << "Couldn't generate a level: " << name;
  return nullptr;

}

PModel ModelBuilder::campaignBaseModel(const string& siteName, TribeId keeperTribe, bool externalEnemies) {
  return tryBuilding(20, [=] { return tryCampaignBaseModel(siteName, keeperTribe, externalEnemies); }, "campaign base");
}

PModel ModelBuilder::tutorialModel(const string& siteName) {
  return tryBuilding(20, [=] { return tryTutorialModel(siteName); }, "tutorial");
}

PModel ModelBuilder::campaignSiteModel(const string& siteName, EnemyId enemyId, VillainType type) {
  return tryBuilding(20, [&] { return tryCampaignSiteModel(siteName, enemyId, type); },
      EnumInfo<EnemyId>::getString(enemyId));
}

void ModelBuilder::measureSiteGen(int numTries, vector<string> types) {
  if (types.empty()) {
    types = {"single_map", "campaign_base", "tutorial"};
    for (auto id : ENUM_ALL(EnemyId)) {
      auto enemy = enemyFactory->get(id);
      if (!!getBiome(enemy, random))
        types.push_back(EnumInfo<EnemyId>::getString(id));
    }
  }
  vector<function<void()>> tasks;
  auto tribe = TribeId::getDarkKeeper();
  for (auto& type : types) {
    if (type == "single_map")
      tasks.push_back([=] { measureModelGen(type, numTries, [&] { trySingleMapModel("pok", tribe); }); });
    else if (type == "campaign_base")
      tasks.push_back([=] { measureModelGen(type, numTries, [&] { tryCampaignBaseModel("pok", tribe, false); }); });
    else if (type == "tutorial")
      tasks.push_back([=] { measureModelGen(type, numTries, [&] { tryTutorialModel("pok"); }); });
    else if (auto id = EnumInfo<EnemyId>::fromStringSafe(type)) {
      tasks.push_back([=] { measureModelGen(type, numTries, [&] { tryCampaignSiteModel("", *id, VillainType::LESSER); }); });
    } else {
      std::cout << "Bad map type: " << type << std::endl;
      return;
    }
  }
  for (auto& t : tasks)
    t();
}

void ModelBuilder::measureModelGen(const string& name, int numTries, function<void()> genFun) {
  int numSuccess = 0;
  int maxT = 0;
  int minT = 1000000;
  double sumT = 0;
  std::cout << name;
  for (int i : Range(numTries)) {
#ifndef OSX // this triggers some compiler errors OSX, I don't need it there anyway.
    auto time = steady_clock::now();
#endif
    try {
      genFun();
      ++numSuccess;
      std::cout << ".";
      std::cout.flush();
    } catch (LevelGenException) {
      std::cout << "x";
      std::cout.flush();
    }
#ifndef OSX
    int millis = duration_cast<milliseconds>(steady_clock::now() - time).count();
    sumT += millis;
    maxT = max(maxT, millis);
    minT = min(minT, millis);
#endif
  }
  std::cout << std::endl << numSuccess << " / " << numTries << ". MinT: " <<
    minT << ". MaxT: " << maxT << ". AvgT: " << sumT / numTries << std::endl;
}

WCollective ModelBuilder::spawnKeeper(WModel m, AvatarInfo avatarInfo, bool regenerateMana, vector<string> introText) {
  WLevel level = m->getTopLevel();
  WCreature keeperRef = avatarInfo.playerCreature.get();
  CHECK(level->landCreature(StairKey::keeperSpawn(), keeperRef)) << "Couldn't place keeper on level.";
  m->addCreature(std::move(avatarInfo.playerCreature));
  auto keeperInfo = avatarInfo.creatureInfo.getReferenceMaybe<KeeperCreatureInfo>();
  m->collectives.push_back(CollectiveBuilder(
        getKeeperConfig(random, options->getBoolValue(OptionId::FAST_IMMIGRATION),
            regenerateMana), keeperRef->getTribeId())
      .setLevel(level)
      .addCreature(keeperRef, {MinionTrait::LEADER})
      .build());
  WCollective playerCollective = m->collectives.back().get();
  auto playerControl = PlayerControl::create(playerCollective, introText, *keeperInfo);
  auto playerControlRef = playerControl.get();
  playerCollective->setControl(std::move(playerControl));
  playerCollective->setVillainType(VillainType::PLAYER);
  for (auto tech : keeperInfo->initialTech)
    playerCollective->acquireTech(Technology::get(tech), false);
  if (auto error = playerControlRef->reloadImmigrationAndWorkshops(gameConfig))
    USER_FATAL << *error;
  return playerCollective;
}

PModel ModelBuilder::tryModel(int width, const string& levelName, vector<EnemyInfo> enemyInfo,
    optional<TribeId> keeperTribe, BiomeId biomeId, optional<ExternalEnemies> externalEnemies, bool hasWildlife) {
  auto model = Model::create();
  vector<SettlementInfo> topLevelSettlements;
  vector<EnemyInfo> extraEnemies;
  for (auto& elem : enemyInfo) {
    elem.settlement.collective = new CollectiveBuilder(elem.config, elem.settlement.tribe);
    if (elem.levelConnection) {
      elem.levelConnection->otherEnemy->settlement.collective =
          new CollectiveBuilder(elem.levelConnection->otherEnemy->config,
                                elem.levelConnection->otherEnemy->settlement.tribe);
      topLevelSettlements.push_back(makeExtraLevel(model.get(), elem));
      extraEnemies.push_back(*elem.levelConnection->otherEnemy);
    } else
      topLevelSettlements.push_back(elem.settlement);
  }
  append(enemyInfo, extraEnemies);
  optional<CreatureFactory> wildlife;
  if (hasWildlife)
    wildlife = CreatureFactory::forrest(TribeId::getWildlife());
  WLevel top =  model->buildTopLevel(
      LevelBuilder(meter, random, width, width, levelName, false),
      LevelMaker::topLevel(random, wildlife, topLevelSettlements, width,
        keeperTribe, biomeId));
  model->calculateStairNavigation();
  for (auto& enemy : enemyInfo) {
    if (enemy.settlement.locationName)
      enemy.settlement.collective->setLocationName(*enemy.settlement.locationName);
    if (auto race = enemy.settlement.race)
      enemy.settlement.collective->setRaceName(*race);
    if (enemy.discoverable)
      enemy.settlement.collective->setDiscoverable();
    PCollective collective = enemy.settlement.collective->build();
    collective->setImmigration(makeOwner<Immigration>(collective.get(), std::move(enemy.immigrants)));
    auto control = VillageControl::create(collective.get(), enemy.villain);
    if (enemy.villainType)
      collective->setVillainType(*enemy.villainType);
    if (enemy.id)
      collective->setEnemyId(*enemy.id);
    collective->setControl(std::move(control));
    model->collectives.push_back(std::move(collective));
  }
  if (externalEnemies)
    model->addExternalEnemies(std::move(*externalEnemies));
  return model;
}

PModel ModelBuilder::splashModel(const FilePath& splashPath) {
  auto m = Model::create();
  WLevel l = m->buildTopLevel(
      LevelBuilder(meter, Random, Level::getSplashBounds().width(), Level::getSplashBounds().height(), "Splash",
        true, 1.0),
      LevelMaker::splashLevel(
          CreatureFactory::splashLeader(TribeId::getHuman()),
          CreatureFactory::splashHeroes(TribeId::getHuman()),
          CreatureFactory::splashMonsters(TribeId::getDarkKeeper()),
          CreatureFactory::singleType(TribeId::getDarkKeeper(), CreatureId::IMP), splashPath));
  m->topLevel = l;
  return m;
}

PModel ModelBuilder::battleModel(const FilePath& levelPath, CreatureList allies, CreatureList enemies) {
  auto m = Model::create();
  ifstream stream(levelPath.getPath());
  Table<char> level = *SokobanInput::readTable(stream);
  WLevel l = m->buildTopLevel(
      LevelBuilder(meter, Random, level.getBounds().width(), level.getBounds().height(), "Battle", true, 1.0),
      LevelMaker::battleLevel(level, allies, enemies));
  m->topLevel = l;
  return m;
}
