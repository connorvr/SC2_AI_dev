#include <iostream>

#include "zergbot.h"

#include <iostream>
#include <string>
#include <algorithm>
#include <random>
#include <iterator>

#include "sc2api/sc2_api.h"
#include "sc2lib/sc2_lib.h"

using namespace sc2{
	struct IsVisible {
		bool operator()(const Unit& unit) { return unit.display_type == Unit::Visible; };
	};
	struct IsAttackable {
		bool operator()(const Unit& unit) {
			switch (unit.unit_type.ToType()) {
			case UNIT_TYPEID::ZERG_OVERLORD: return false;
			case UNIT_TYPEID::ZERG_OVERSEER: return false;
			case UNIT_TYPEID::PROTOSS_OBSERVER: return false;
			default: return true;
			}
		}
	};

	struct IsFlying {
		bool operator()(const Unit& unit) {
			return unit.is_flying;
		}
	};

	//Ignores Overlords, workers, and structures
	struct IsArmy {
		IsArmy(const ObservationInterface* obs) : observation_(obs) {}

		bool operator()(const Unit& unit) {
			auto attributes = observation_->GetUnitTypeData().at(unit.unit_type).attributes;
			for (const auto& attribute : attributes) {
				if (attribute == Attribute::Structure) {
					return false;
				}
			}
			switch (unit.unit_type.ToType()) {
			case UNIT_TYPEID::ZERG_OVERLORD: return false;
			case UNIT_TYPEID::PROTOSS_PROBE: return false;
			case UNIT_TYPEID::ZERG_DRONE: return false;
			case UNIT_TYPEID::TERRAN_SCV: return false;
			case UNIT_TYPEID::ZERG_QUEEN: return false;
			case UNIT_TYPEID::ZERG_LARVA: return false;
			case UNIT_TYPEID::ZERG_EGG: return false;
			case UNIT_TYPEID::TERRAN_MULE: return false;
			case UNIT_TYPEID::TERRAN_NUKE: return false;
			default: return true;
			}
		}

		const ObservationInterface* observation_;
	};

	struct IsTownHall {
		bool operator()(const Unit& unit) {
			switch (unit.unit_type.ToType()) {
			case UNIT_TYPEID::ZERG_HATCHERY: return true;
			case UNIT_TYPEID::ZERG_LAIR: return true;
			case UNIT_TYPEID::ZERG_HIVE: return true;
			case UNIT_TYPEID::TERRAN_COMMANDCENTER: return true;
			case UNIT_TYPEID::TERRAN_ORBITALCOMMAND: return true;
			case UNIT_TYPEID::TERRAN_ORBITALCOMMANDFLYING: return true;
			case UNIT_TYPEID::TERRAN_PLANETARYFORTRESS: return true;
			case UNIT_TYPEID::PROTOSS_NEXUS: return true;
			default: return false;
			}
		}
	};

	struct IsVespeneGeyser {
		bool operator()(const Unit& unit) {
			switch (unit.unit_type.ToType()) {
			case UNIT_TYPEID::NEUTRAL_VESPENEGEYSER: return true;
			case UNIT_TYPEID::NEUTRAL_SPACEPLATFORMGEYSER: return true;
			case UNIT_TYPEID::NEUTRAL_PROTOSSVESPENEGEYSER: return true;
			default: return false;
			}
		}
	};

	struct IsStructure {
		IsStructure(const ObservationInterface* obs) : observation_(obs) {};

		bool operator()(const Unit& unit) {
			auto& attributes = observation_->GetUnitTypeData().at(unit.unit_type).attributes;
			bool is_structure = false;
			for (const auto& attribute : attributes) {
				if (attribute == Attribute::Structure) {
					is_structure = true;
				}
			}
			return is_structure;
		}

		const ObservationInterface* observation_;
	};

	int CountUnitType(const ObservationInterface* observation, UnitTypeID unit_type) {
		int count = 0;
		Units my_units = observation->GetUnits(Unit::Alliance::Self);
		for (const Unit& unit : my_units) {
			if (unit.unit_type == unit_type)
				++count;
		}

		return count;
	}

	bool FindEnemyStructure(const ObservationInterface* observation, Unit& enemy_unit) {
		Units my_units = observation->GetUnits(Unit::Alliance::Enemy);
		for (const Unit& unit : my_units) {
			if (unit.unit_type == UNIT_TYPEID::TERRAN_COMMANDCENTER ||
				unit.unit_type == UNIT_TYPEID::TERRAN_SUPPLYDEPOT ||
				unit.unit_type == UNIT_TYPEID::TERRAN_BARRACKS) {
				enemy_unit = unit;
				return true;
			}
		}

		return false;
	}

	bool GetRandomUnit(Unit& unit_out, const ObservationInterface* observation, UnitTypeID unit_type) {
		Units my_units = observation->GetUnits(Unit::Alliance::Self);
		std::random_shuffle(my_units.begin(), my_units.end()); // Doesn't work, or doesn't work well.
		for (const Unit& unit : my_units) {
			if (unit.unit_type == unit_type) {
				unit_out = unit;
				return true;
			}
		}
		return false;
	}

	void MultiplayerBot::PrintStatus(std::string msg) {
		int64_t bot_identifier = int64_t(this) & 0xFFFLL;
		std::cout << std::to_string(bot_identifier) << ": " << msg << std::endl;
	}

	void MultiplayerBot::OnGameStart() {
		game_info_ = Observation()->GetGameInfo();
		PrintStatus("game started.");
		expansions_ = search::CalculateExpansionLocations(Observation(), Query());

		//Temporary, we can replace this with observation->GetStartLocation() once implemented
		startLocation_ = Observation()->GetStartLocation();
		staging_location_ = startLocation_;
	};

	size_t MultiplayerBot::CountUnitType(const ObservationInterface* observation, UnitTypeID unit_type) {
		return observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type)).size();
	}

	size_t MultiplayerBot::CountUnitTypeBuilding(const ObservationInterface* observation, UNIT_TYPEID production_building, ABILITY_ID ability) {
		int building_count = 0;
		Units buildings = observation->GetUnits(Unit::Self, IsUnit(production_building));

		for (const auto& building : buildings) {
			if (building.orders.empty()) {
				continue;
			}

			for (const auto order : building.orders) {
				if (order.ability_id == ability) {
					building_count++;
				}
			}
		}

		return building_count;
	}

	size_t MultiplayerBot::CountUnitTypeTotal(const ObservationInterface* observation, UNIT_TYPEID unit_type, UNIT_TYPEID production, ABILITY_ID ability) {
		return CountUnitType(observation, unit_type) + CountUnitTypeBuilding(observation, production, ability);
	}

	size_t MultiplayerBot::CountUnitTypeTotal(const ObservationInterface* observation, std::vector<UNIT_TYPEID> unit_type, UNIT_TYPEID production, ABILITY_ID ability) {
		size_t count = 0;
		for (const auto& type : unit_type) {
			count += CountUnitType(observation, type);
		}
		return count + CountUnitTypeBuilding(observation, production, ability);
	}

	bool MultiplayerBot::GetRandomUnit(Unit& unit_out, const ObservationInterface* observation, UnitTypeID unit_type) {
		Units my_units = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
		if (!my_units.empty()) {
			unit_out = GetRandomEntry(my_units);
			return true;
		}
		return false;
	}

	bool MultiplayerBot::FindNearestMineralPatch(const Point2D& start, Tag& target) {
		Units units = Observation()->GetUnits(Unit::Alliance::Neutral);
		float distance = std::numeric_limits<float>::max();
		for (const auto& u : units) {
			if (u.unit_type == UNIT_TYPEID::NEUTRAL_MINERALFIELD) {
				float d = DistanceSquared2D(u.pos, start);
				if (d < distance) {
					distance = d;
					target = u.tag;
				}
			}
		}
		//If we never found one return false;
		if (distance == std::numeric_limits<float>::max()) {
			return false;
		}
		return true;
	}

	// Tries to find a random location that can be pathed to on the map.
	// Returns 'true' if a new, random location has been found that is pathable by the unit.
	bool MultiplayerBot::FindEnemyPosition(Tag, Point2D& target_pos) {
		if (game_info_.enemy_start_locations.empty()) {
			return false;
		}
		target_pos = game_info_.enemy_start_locations.front();
		return true;
	}

	bool MultiplayerBot::TryFindRandomPathableLocation(Tag tag, Point2D& target_pos) {
		// First, find a random point inside the playable area of the map.
		float playable_w = game_info_.playable_max.x - game_info_.playable_min.x;
		float playable_h = game_info_.playable_max.y - game_info_.playable_min.y;

		// The case where game_info_ does not provide a valid answer
		if (playable_w == 0 || playable_h == 0) {
			playable_w = 236;
			playable_h = 228;
		}

		target_pos.x = playable_w * GetRandomFraction() + game_info_.playable_min.x;
		target_pos.y = playable_h * GetRandomFraction() + game_info_.playable_min.y;

		// Now send a pathing query from the unit to that point. Can also query from point to point,
		// but using a unit tag wherever possible will be more accurate.
		// Note: This query must communicate with the game to get a result which affects performance.
		// Ideally batch up the queries (using PathingDistanceBatched) and do many at once.
		float distance = Query()->PathingDistance(tag, target_pos);

		return distance > 0.1f;
	}

	void MultiplayerBot::AttackWithUnitType(UnitTypeID unit_type, const ObservationInterface* observation) {
		Units units = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
		for (const auto& unit : units) {
			AttackWithUnit(unit, observation);
		}
	}

	void MultiplayerBot::AttackWithUnit(Unit unit, const ObservationInterface* observation) {
		//If unit isn't doing anything make it attack.
		Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy);
		if (enemy_units.empty()) {
			return;
		}

		if (unit.orders.empty()) {
			Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, enemy_units.front().pos);
			return;
		}

		//If the unit is doing something besides attacking, make it attack.
		if (unit.orders.front().ability_id != ABILITY_ID::ATTACK) {
			Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, enemy_units.front().pos);
		}
	}

	void MultiplayerBot::ScoutWithUnits(UnitTypeID unit_type, const ObservationInterface* observation) {
		Units units = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
		for (const auto& unit : units) {
			ScoutWithUnit(unit, observation);
		}
	}

	void MultiplayerBot::ScoutWithUnit(Unit unit, const ObservationInterface* observation) {
		Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy, IsAttackable());
		if (!unit.orders.empty()) {
			return;
		}
		Point2D target_pos;

		if (FindEnemyPosition(unit.tag, target_pos)) {
			if (Distance2D(unit.pos, target_pos) < 20 && enemy_units.empty()) {
				if (TryFindRandomPathableLocation(unit.tag, target_pos)) {
					Actions()->UnitCommand(unit, ABILITY_ID::SMART, target_pos);
					return;
				}
			}
			else if (!enemy_units.empty())
			{
				Actions()->UnitCommand(unit, ABILITY_ID::ATTACK, enemy_units.front());
				return;
			}
			Actions()->UnitCommand(unit, ABILITY_ID::SMART, target_pos);
		}
		else {
			if (TryFindRandomPathableLocation(unit.tag, target_pos)) {
				Actions()->UnitCommand(unit, ABILITY_ID::SMART, target_pos);
			}
		}
	}

	//Try build structure given a location. This is used most of the time
	bool MultiplayerBot::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Point2D location, bool isExpansion = false) {

		const ObservationInterface* observation = Observation();
		Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));

		//if we have no workers Don't build
		if (workers.empty()) {
			return false;
		}

		// Check to see if there is already a worker heading out to build it
		for (const auto& worker : workers) {
			for (const auto& order : worker.orders) {
				if (order.ability_id == ability_type_for_structure) {
					return false;
				}
			}
		}

		// If no worker is already building one, get a random worker to build one
		const Unit& unit = GetRandomEntry(workers);

		// Check to see if unit can make it there
		if (Query()->PathingDistance(unit.tag, location) < 0.1f) {
			return false;
		}
		if (!isExpansion) {
			for (const auto& expansion : expansions_) {
				if (Distance2D(location, Point2D(expansion.x, expansion.y)) < 7) {
					return false;
				}
			}
		}
		// Check to see if unit can build there
		if (Query()->Placement(ability_type_for_structure, location)) {
			Actions()->UnitCommand(unit.tag, ability_type_for_structure, location);
			return true;
		}
		return false;

	}

	//Try to build a structure based on tag, Used mostly for Vespene, since the pathing check will fail even though the geyser is "Pathable"
	bool MultiplayerBot::TryBuildStructure(AbilityID ability_type_for_structure, UnitTypeID unit_type, Tag location_tag) {
		const ObservationInterface* observation = Observation();
		Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
		const Unit* target = observation->GetUnit(location_tag);

		if (workers.empty()) {
			return false;
		}

		// Check to see if there is already a worker heading out to build it
		for (const auto& worker : workers) {
			for (const auto& order : worker.orders) {
				if (order.ability_id == ability_type_for_structure) {
					return false;
				}
			}
		}

		// If no worker is already building one, get a random worker to build one
		const Unit& unit = GetRandomEntry(workers);

		// Check to see if unit can build there
		if (Query()->Placement(ability_type_for_structure, target->pos)) {
			Actions()->UnitCommand(unit.tag, ability_type_for_structure, location_tag);
			return true;
		}
		return false;

	}

	//Expands to nearest location and updates the start location to be between the new location and old bases.
	bool MultiplayerBot::TryExpand(AbilityID build_ability, UnitTypeID worker_type) {
		const ObservationInterface* observation = Observation();
		float minimum_distance = std::numeric_limits<float>::max();
		Point3D closest_expansion;
		for (const auto& expansion : expansions_) {
			float current_distance = Distance2D(startLocation_, expansion);
			if (current_distance < .01f) {
				continue;
			}

			if (current_distance < minimum_distance) {
				if (Query()->Placement(build_ability, expansion)) {
					closest_expansion = expansion;
					minimum_distance = current_distance;
				}
			}
		}
		//only update staging location up till 3 bases.
		if (TryBuildStructure(build_ability, worker_type, closest_expansion, true) && observation->GetUnits(Unit::Self, IsTownHall()).size() < 4) {
			staging_location_ = Point3D(((staging_location_.x + closest_expansion.x) / 2), ((staging_location_.y + closest_expansion.y) / 2),
				((staging_location_.z + closest_expansion.z) / 2));
			return true;
		}
		return false;

	}

	//Tries to build a geyser for a base
	bool MultiplayerBot::TryBuildGas(AbilityID build_ability, UnitTypeID worker_type, Point2D base_location) {
		const ObservationInterface* observation = Observation();
		Units geysers = observation->GetUnits(Unit::Alliance::Neutral, IsVespeneGeyser());

		//only search within this radius
		float minimum_distance = 15.0f;
		Tag closestGeyser = 0;
		for (const auto& geyser : geysers) {
			float current_distance = Distance2D(base_location, geyser.pos);
			if (current_distance < minimum_distance) {
				if (Query()->Placement(build_ability, geyser.pos)) {
					minimum_distance = current_distance;
					closestGeyser = geyser.tag;
				}
			}
		}

		// In the case where there are no more available geysers nearby
		if (closestGeyser == 0) {
			return false;
		}
		return TryBuildStructure(build_ability, worker_type, closestGeyser);

	}

	bool MultiplayerBot::TryBuildUnit(AbilityID ability_type_for_unit, UnitTypeID unit_type) {
		const ObservationInterface* observation = Observation();

		//If we are at supply cap, don't build anymore units, unless its an overlord.
		if (observation->GetFoodUsed() >= observation->GetFoodCap() && ability_type_for_unit != ABILITY_ID::TRAIN_OVERLORD) {
			return false;
		}
		Unit unit;
		if (!GetRandomUnit(unit, observation, unit_type)) {
			return false;
		}
		if (!unit.orders.empty()) {
			return false;
		}

		if (unit.build_progress != 1) {
			return false;
		}

		Actions()->UnitCommand(unit.tag, ability_type_for_unit);
		return true;
	}

	// Mine the nearest mineral to Town hall.
	// If we don't do this, probes may mine from other patches if they stray too far from the base after building.
	void MultiplayerBot::MineIdleWorkers(Tag worker_tag, AbilityID worker_gather_command, UnitTypeID vespene_building_type) {
		const ObservationInterface* observation = Observation();
		Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
		Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));

		Tag valid_mineral_patch;

		if (bases.empty()) {
			return;
		}

		for (const auto& geyser : geysers) {
			if (geyser.assigned_harvesters < geyser.ideal_harvesters) {
				Actions()->UnitCommand(worker_tag, worker_gather_command, geyser.tag);
				return;
			}
		}
		//Search for a base that is missing workers.
		for (const auto& base : bases) {
			//If we have already mined out here skip the base.
			if (base.ideal_harvesters == 0 || base.build_progress != 1) {
				continue;
			}
			if (base.assigned_harvesters < base.ideal_harvesters) {
				FindNearestMineralPatch(base.pos, valid_mineral_patch);
				Actions()->UnitCommand(worker_tag, worker_gather_command, valid_mineral_patch);
				return;
			}
		}

		if (!observation->GetUnit(worker_tag)->orders.empty()) {
			return;
		}

		//If all workers are spots are filled just go to any base.
		const Unit& random_base = GetRandomEntry(bases);
		FindNearestMineralPatch(random_base.pos, valid_mineral_patch);
		Actions()->UnitCommand(worker_tag, worker_gather_command, valid_mineral_patch);
	}

	//An estimate of how many workers we should have based on what buildings we have
	int MultiplayerBot::GetExpectedWorkers(UNIT_TYPEID vespene_building_type) {
		const ObservationInterface* observation = Observation();
		Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
		Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));
		int expected_workers = 0;
		for (const auto& base : bases) {
			if (base.build_progress != 1) {
				continue;
			}
			expected_workers += base.ideal_harvesters;
		}

		for (const auto& geyser : geysers) {
			if (geyser.vespene_contents > 0) {
				if (geyser.build_progress != 1) {
					continue;
				}
				expected_workers += geyser.ideal_harvesters;
			}
		}

		return expected_workers;
	}

	// To ensure that we do not over or under saturate any base.
	void MultiplayerBot::ManageWorkers(UNIT_TYPEID worker_type, AbilityID worker_gather_command, UNIT_TYPEID vespene_building_type) {
		const ObservationInterface* observation = Observation();
		Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
		Units geysers = observation->GetUnits(Unit::Alliance::Self, IsUnit(vespene_building_type));

		if (bases.empty()) {
			return;
		}

		for (const auto& base : bases) {
			//If we have already mined out or still building here skip the base.
			if (base.ideal_harvesters == 0 || base.build_progress != 1) {
				continue;
			}
			//if base is
			if (base.assigned_harvesters > base.ideal_harvesters) {
				Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));

				for (const auto& worker : workers) {
					if (!worker.orders.empty()) {
						if (worker.orders.front().target_unit_tag == base.tag) {
							//This should allow them to be picked up by mineidleworkers()
							MineIdleWorkers(worker.tag, worker_gather_command, vespene_building_type);
							return;
						}
					}
				}
			}
		}
		Units workers = observation->GetUnits(Unit::Alliance::Self, IsUnit(worker_type));
		for (const auto& geyser : geysers) {
			if (geyser.ideal_harvesters == 0 || geyser.build_progress != 1) {
				continue;
			}
			if (geyser.assigned_harvesters > geyser.ideal_harvesters) {
				for (const auto& worker : workers) {
					if (!worker.orders.empty()) {
						if (worker.orders.front().target_unit_tag == geyser.tag) {
							//This should allow them to be picked up by mineidleworkers()
							MineIdleWorkers(worker.tag, worker_gather_command, vespene_building_type);
							return;
						}
					}
				}
			}
			else if (geyser.assigned_harvesters < geyser.ideal_harvesters) {
				for (const auto& worker : workers) {
					if (!worker.orders.empty()) {
						//This should move a worker that isn't mining gas to gas
						const Unit* target = observation->GetUnit(worker.orders.front().target_unit_tag);
						if (target == nullptr) {
							continue;
						}
						if (target->unit_type != vespene_building_type) {
							//This should allow them to be picked up by mineidleworkers()
							MineIdleWorkers(worker.tag, worker_gather_command, vespene_building_type);
							return;
						}
					}
				}
			}
		}
	}

	void MultiplayerBot::RetreatWithUnits(UnitTypeID unit_type, Point2D retreat_position) {
		const ObservationInterface* observation = Observation();
		Units units = observation->GetUnits(Unit::Alliance::Self, IsUnit(unit_type));
		for (const auto& unit : units) {
			RetreatWithUnit(unit, retreat_position);
		}
	}

	void MultiplayerBot::RetreatWithUnit(Unit unit, Point2D retreat_position) {
		float dist = Distance2D(unit.pos, retreat_position);

		if (dist < 10) {
			if (unit.orders.empty()) {
				return;
			}
			Actions()->UnitCommand(unit, ABILITY_ID::STOP);
			return;
		}

		if (unit.orders.empty() && dist > 14) {
			Actions()->UnitCommand(unit, ABILITY_ID::MOVE, retreat_position);
		}
		else if (!unit.orders.empty() && dist > 14) {
			if (unit.orders.front().ability_id != ABILITY_ID::MOVE) {
				Actions()->UnitCommand(unit, ABILITY_ID::MOVE, retreat_position);
			}
		}
	}

	void MultiplayerBot::OnNuclearLaunchDetected() {
		const ObservationInterface* observation = Observation();
		nuke_detected = true;
		nuke_detected_frame = observation->GetGameLoop();
	}





	bool ZergMultiplayerBot::TryBuildDrone() {
		const ObservationInterface* observation = Observation();
		size_t larva_count = CountUnitType(observation, UNIT_TYPEID::ZERG_LARVA);
		Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());
		size_t worker_count = CountUnitType(observation, UNIT_TYPEID::ZERG_DRONE);
		Units eggs = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EGG));
		for (const auto& egg : eggs) {
			if (!egg.orders.empty()) {
				if (egg.orders.front().ability_id == ABILITY_ID::TRAIN_DRONE) {
					worker_count++;
				}
			}
		}
		if (worker_count >= max_worker_count_) {
			return false;
		}

		if (worker_count > GetExpectedWorkers(UNIT_TYPEID::ZERG_EXTRACTOR)) {
			return false;
		}

		if (observation->GetFoodUsed() >= observation->GetFoodCap()) {
			return false;
		}

		for (const auto& base : bases) {
			//if there is a base with less than ideal workers
			if (base.assigned_harvesters < base.ideal_harvesters && base.build_progress == 1) {
				if (observation->GetMinerals() >= 50 && larva_count > 0) {
					Tag mineral;
					FindNearestMineralPatch(base.pos, mineral);
					return TryBuildUnit(ABILITY_ID::TRAIN_DRONE, UNIT_TYPEID::ZERG_LARVA);
				}
			}
		}
		return false;
	}

	bool ZergMultiplayerBot::TryBuildOnCreep(AbilityID ability_type_for_structure, UnitTypeID unit_type) {
		float rx = GetRandomScalar();
		float ry = GetRandomScalar();
		const ObservationInterface* observation = Observation();
		Point2D build_location = Point2D(startLocation_.x + rx * 15, startLocation_.y + ry * 15);

		if (observation->HasCreep(build_location)) {
			return TryBuildStructure(ability_type_for_structure, unit_type, build_location);
		}
		return false;
	}

	void ZergMultiplayerBot::BuildOrder() {
		const ObservationInterface* observation = Observation();
		bool hive_tech = CountUnitType(observation, UNIT_TYPEID::ZERG_HIVE) > 0;
		bool lair_tech = CountUnitType(observation, UNIT_TYPEID::ZERG_LAIR) > 0 || hive_tech;
		size_t base_count = observation->GetUnits(Unit::Self, IsTownHall()).size();
		size_t evolution_chanber_target = 1;
		size_t morphing_lair = 0;
		size_t morphing_hive = 0;
		Units hatcherys = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_HATCHERY));
		Units lairs = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_LAIR));
		for (const auto& hatchery : hatcherys) {
			if (!hatchery.orders.empty()) {
				if (hatchery.orders.front().ability_id == ABILITY_ID::MORPH_LAIR) {
					++morphing_lair;
				}
			}
		}
		for (const auto& lair : lairs) {
			if (!lair.orders.empty()) {
				if (lair.orders.front().ability_id == ABILITY_ID::MORPH_HIVE) {
					++morphing_hive;
				}
			}
		}

		if (!mutalisk_build_) {
			evolution_chanber_target++;
		}
		//Priority to spawning pool
		if (CountUnitType(observation, UNIT_TYPEID::ZERG_SPAWNINGPOOL) < 1) {
			TryBuildOnCreep(ABILITY_ID::BUILD_SPAWNINGPOOL, UNIT_TYPEID::ZERG_DRONE);
		}
		else {
			if (base_count < 1) {
				TryBuildExpansionHatch();
				return;
			}

			if (CountUnitType(observation, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER) < evolution_chanber_target) {
				TryBuildOnCreep(ABILITY_ID::BUILD_EVOLUTIONCHAMBER, UNIT_TYPEID::ZERG_DRONE);
			}

			if (!mutalisk_build_ && CountUnitType(observation, UNIT_TYPEID::ZERG_ROACHWARREN) < 1) {
				TryBuildOnCreep(ABILITY_ID::BUILD_ROACHWARREN, UNIT_TYPEID::ZERG_DRONE);
			}

			if (!lair_tech) {
				if (CountUnitType(observation, UNIT_TYPEID::ZERG_HIVE) + CountUnitType(observation, UNIT_TYPEID::ZERG_LAIR) < 1 && CountUnitType(observation, UNIT_TYPEID::ZERG_QUEEN) > 0) {
					TryBuildUnit(ABILITY_ID::MORPH_LAIR, UNIT_TYPEID::ZERG_HATCHERY);
				}
			}
			else {
				if (!mutalisk_build_) {
					if (CountUnitType(observation, UNIT_TYPEID::ZERG_HYDRALISKDEN) + CountUnitType(observation, UNIT_TYPEID::ZERG_LURKERDENMP) < 1) {
						TryBuildOnCreep(ABILITY_ID::BUILD_HYDRALISKDEN, UNIT_TYPEID::ZERG_DRONE);
					}
					if (CountUnitType(observation, UNIT_TYPEID::ZERG_HYDRALISKDEN) > 0) {
						TryBuildUnit(ABILITY_ID::MORPH_LURKERDEN, UNIT_TYPEID::ZERG_HYDRALISKDEN);
					}
				}
				else {
					if (CountUnitType(observation, UNIT_TYPEID::ZERG_SPIRE) + CountUnitType(observation, UNIT_TYPEID::ZERG_GREATERSPIRE) < 1) {
						TryBuildOnCreep(ABILITY_ID::BUILD_SPIRE, UNIT_TYPEID::ZERG_DRONE);
					}
				}

				if (base_count < 3) {
					TryBuildExpansionHatch();
					return;
				}
				if (CountUnitType(observation, UNIT_TYPEID::ZERG_INFESTATIONPIT) > 0 && CountUnitType(observation, UNIT_TYPEID::ZERG_HIVE) < 1) {
					TryBuildUnit(ABILITY_ID::MORPH_HIVE, UNIT_TYPEID::ZERG_LAIR);
					return;
				}

				if (CountUnitType(observation, UNIT_TYPEID::ZERG_BANELINGNEST) < 1) {
					TryBuildOnCreep(ABILITY_ID::BUILD_BANELINGNEST, UNIT_TYPEID::ZERG_DRONE);
				}

				if (observation->GetUnits(Unit::Self, IsTownHall()).size() > 2) {
					if (CountUnitType(observation, UNIT_TYPEID::ZERG_INFESTATIONPIT) < 1) {
						TryBuildOnCreep(ABILITY_ID::BUILD_INFESTATIONPIT, UNIT_TYPEID::ZERG_DRONE);
					}
				}

			}

			if (hive_tech) {

				if (!mutalisk_build_ && CountUnitType(observation, UNIT_TYPEID::ZERG_ULTRALISKCAVERN) < 1) {
					TryBuildOnCreep(ABILITY_ID::BUILD_ULTRALISKCAVERN, UNIT_TYPEID::ZERG_DRONE);
				}

				if (CountUnitType(observation, UNIT_TYPEID::ZERG_GREATERSPIRE) < 1) {
					TryBuildUnit(ABILITY_ID::MORPH_GREATERSPIRE, UNIT_TYPEID::ZERG_SPIRE);
				}
			}

		}
	}

	void ZergMultiplayerBot::ManageArmy() {
		const ObservationInterface* observation = Observation();

		Units enemy_units = observation->GetUnits(Unit::Alliance::Enemy);
		Units army = observation->GetUnits(Unit::Alliance::Self, IsArmy(observation));
		int wait_til_supply = 100;

		if (enemy_units.empty() && observation->GetFoodArmy() < wait_til_supply) {
			for (const auto& unit : army) {
				switch (unit.unit_type.ToType()) {
				case(UNIT_TYPEID::ZERG_LURKERMPBURROWED) : {
															   Actions()->UnitCommand(unit.tag, ABILITY_ID::BURROWUP);
				}
				default:
					RetreatWithUnit(unit, staging_location_);
					break;
				}
			}
		}
		else if (!enemy_units.empty()) {
			for (const auto& unit : army) {
				switch (unit.unit_type.ToType()) {
				case(UNIT_TYPEID::ZERG_CORRUPTOR) : {
														Tag closest_unit;
														float distance = std::numeric_limits<float>::max();
														for (const auto& u : enemy_units) {
															float d = Distance2D(u.pos, unit.pos);
															if (d < distance) {
																distance = d;
																closest_unit = u.tag;
															}
														}
														const Unit* enemy_unit = observation->GetUnit(closest_unit);

														auto attributes = observation->GetUnitTypeData().at(enemy_unit->unit_type).attributes;
														for (const auto& attribute : attributes) {
															if (attribute == Attribute::Structure) {
																Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_CAUSTICSPRAY, enemy_unit->tag);
															}
														}
														if (!unit.orders.empty()) {
															if (unit.orders.front().ability_id == ABILITY_ID::EFFECT_CAUSTICSPRAY) {
																break;
															}
														}
														AttackWithUnit(unit, observation);
														break;
				}
				case(UNIT_TYPEID::ZERG_OVERSEER) : {
													   Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, enemy_units.front());
													   break;
				}
				case(UNIT_TYPEID::ZERG_RAVAGER) : {
													  Point2D closest_unit;
													  float distance = std::numeric_limits<float>::max();
													  for (const auto& u : enemy_units) {
														  float d = Distance2D(u.pos, unit.pos);
														  if (d < distance) {
															  distance = d;
															  closest_unit = u.pos;
														  }
													  }
													  Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_CORROSIVEBILE, closest_unit);
													  AttackWithUnit(unit, observation);
				}
				case(UNIT_TYPEID::ZERG_LURKERMP) : {
													   Point2D closest_unit;
													   float distance = std::numeric_limits<float>::max();
													   for (const auto& u : enemy_units) {
														   float d = Distance2D(u.pos, unit.pos);
														   if (d < distance) {
															   distance = d;
															   closest_unit = u.pos;
														   }
													   }
													   if (distance < 7) {
														   Actions()->UnitCommand(unit.tag, ABILITY_ID::BURROWDOWN);
													   }
													   else {
														   Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, closest_unit);
													   }
													   break;
				}
				case(UNIT_TYPEID::ZERG_LURKERMPBURROWED) : {
															   float distance = std::numeric_limits<float>::max();
															   for (const auto& u : enemy_units) {
																   float d = Distance2D(u.pos, unit.pos);
																   if (d < distance) {
																	   distance = d;
																   }
															   }
															   if (distance > 9) {
																   Actions()->UnitCommand(unit.tag, ABILITY_ID::BURROWUP);
															   }
															   break;
				}
				case(UNIT_TYPEID::ZERG_SWARMHOSTMP) : {
														  Point2D closest_unit;
														  float distance = std::numeric_limits<float>::max();
														  for (const auto& u : enemy_units) {
															  float d = Distance2D(u.pos, unit.pos);
															  if (d < distance) {
																  distance = d;
																  closest_unit = u.pos;
															  }
														  }
														  if (distance < 15) {
															  const auto abilities = Query()->GetAbilitiesForUnit(unit.tag).abilities;
															  bool ability_available = false;
															  for (const auto& ability : abilities) {
																  if (ability.ability_id == ABILITY_ID::EFFECT_SPAWNLOCUSTS) {
																	  ability_available = true;
																  }
															  }
															  if (ability_available) {
																  Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_SPAWNLOCUSTS, closest_unit);
															  }
															  else {
																  RetreatWithUnit(unit, staging_location_);
															  }
														  }
														  else {
															  Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, closest_unit);
														  }
														  break;
				}
				case(UNIT_TYPEID::ZERG_INFESTOR) : {
													   Point2D closest_unit;
													   float distance = std::numeric_limits<float>::max();
													   for (const auto& u : enemy_units) {
														   float d = Distance2D(u.pos, unit.pos);
														   if (d < distance) {
															   distance = d;
															   closest_unit = u.pos;
														   }
													   }
													   if (distance < 9) {
														   const auto abilities = Query()->GetAbilitiesForUnit(unit.tag).abilities;
														   if (unit.energy > 75) {
															   Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_FUNGALGROWTH, closest_unit);
														   }
														   else {
															   RetreatWithUnit(unit, staging_location_);
														   }
													   }
													   else {
														   Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, closest_unit);
													   }
													   break;
				}
				case(UNIT_TYPEID::ZERG_VIPER) : {
													Tag closest_unit;
													bool is_flying = false;
													float distance = std::numeric_limits<float>::max();
													for (const auto& u : enemy_units) {
														auto attributes = observation->GetUnitTypeData().at(u.unit_type).attributes;
														bool is_structure = false;
														for (const auto& attribute : attributes) {
															if (attribute == Attribute::Structure) {
																is_structure = true;
															}
														}
														if (is_structure) {
															continue;
														}
														float d = Distance2D(u.pos, unit.pos);
														if (d < distance) {
															distance = d;
															closest_unit = u.tag;
															is_flying = u.is_flying;
														}
													}
													if (distance < 10) {
														if (is_flying && unit.energy > 124) {
															Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_PARASITICBOMB, closest_unit);
														}
														else if (!is_flying && unit.energy > 100) {
															Point2D target = observation->GetUnit(closest_unit)->pos;
															Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_BLINDINGCLOUD, target);
														}
														else {
															RetreatWithUnit(unit, startLocation_);
														}

													}
													else {
														if (unit.energy > 124) {
															Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, enemy_units.front());
														}
														else {
															if (!unit.orders.empty()) {
																if (unit.orders.front().ability_id != ABILITY_ID::EFFECT_VIPERCONSUME) {
																	Units extractors = observation->GetUnits(Unit::Self, IsUnit(UNIT_TYPEID::ZERG_EXTRACTOR));
																	for (const auto& extractor : extractors) {
																		if (extractor.health > 200) {
																			Actions()->UnitCommand(unit.tag, ABILITY_ID::EFFECT_VIPERCONSUME, extractor.tag);
																		}
																		else {
																			continue;
																		}
																	}
																}
																else {
																	if (observation->GetUnit(unit.orders.front().target_unit_tag)->health < 100) {
																		Actions()->UnitCommand(unit.tag, ABILITY_ID::STOP);
																	}
																}
															}
															else {
																Actions()->UnitCommand(unit.tag, ABILITY_ID::ATTACK, closest_unit);
															}
														}
													}
													break;
				}
				default: {
							 AttackWithUnit(unit, observation);
				}
				}
			}
		}
		else {
			for (const auto& unit : army) {
				switch (unit.unit_type.ToType()){
				case(UNIT_TYPEID::ZERG_LURKERMPBURROWED) : {
															   Actions()->UnitCommand(unit.tag, ABILITY_ID::BURROWUP);
				}
				default:
					ScoutWithUnit(unit, observation);
					break;
				}
			}
		}
	}

	void ZergMultiplayerBot::BuildArmy() {
		const ObservationInterface* observation = Observation();
		size_t larva_count = CountUnitType(observation, UNIT_TYPEID::ZERG_LARVA);
		size_t base_count = observation->GetUnits(Unit::Self, IsTownHall()).size();

		size_t queen_Count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_QUEEN, UNIT_TYPEID::ZERG_HATCHERY, ABILITY_ID::TRAIN_QUEEN);
		queen_Count += CountUnitTypeBuilding(observation, UNIT_TYPEID::ZERG_LAIR, ABILITY_ID::TRAIN_QUEEN);
		queen_Count += CountUnitTypeBuilding(observation, UNIT_TYPEID::ZERG_HIVE, ABILITY_ID::TRAIN_QUEEN);
		size_t hydralisk_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_HYDRALISK, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_HYDRALISK);
		size_t roach_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_ROACH, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_ROACH);
		size_t corruptor_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_CORRUPTOR, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_CORRUPTOR);
		size_t swarmhost_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_SWARMHOSTMP, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_SWARMHOST);
		size_t viper_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_VIPER, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_VIPER);
		size_t ultralisk_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_ULTRALISK, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_ULTRALISK);
		size_t infestor_count = CountUnitTypeTotal(observation, UNIT_TYPEID::ZERG_INFESTOR, UNIT_TYPEID::ZERG_EGG, ABILITY_ID::TRAIN_INFESTOR);

		if (queen_Count < base_count && CountUnitType(observation, UNIT_TYPEID::ZERG_SPAWNINGPOOL) > 0) {
			if (observation->GetMinerals() >= 150) {
				if (!TryBuildUnit(ABILITY_ID::TRAIN_QUEEN, UNIT_TYPEID::ZERG_HATCHERY)) {
					if (!TryBuildUnit(ABILITY_ID::TRAIN_QUEEN, UNIT_TYPEID::ZERG_LAIR)) {
						TryBuildUnit(ABILITY_ID::TRAIN_QUEEN, UNIT_TYPEID::ZERG_HIVE);
					}
				}
			}
		}
		if (CountUnitType(observation, UNIT_TYPEID::ZERG_OVERSEER) + CountUnitType(observation, UNIT_TYPEID::ZERG_OVERLORDCOCOON) < 1) {
			TryBuildUnit(ABILITY_ID::MORPH_OVERSEER, UNIT_TYPEID::ZERG_OVERLORD);
		}

		if (larva_count > 0) {
			if (viper_count < 2) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_VIPER, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
			}
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_ULTRALISKCAVERN) > 0 && !mutalisk_build_ && ultralisk_count < 4) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_ULTRALISK, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
				//Try to build at least one ultralisk
				if (ultralisk_count < 1) {
					return;
				}
			}
		}

		if (!mutalisk_build_) {
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_RAVAGER) + CountUnitType(observation, UNIT_TYPEID::ZERG_RAVAGERCOCOON) < 3) {
				TryBuildUnit(ABILITY_ID::MORPH_RAVAGER, UNIT_TYPEID::ZERG_ROACH);
			}
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_LURKERMP) + CountUnitType(observation, UNIT_TYPEID::ZERG_LURKERMPEGG) + CountUnitType(observation, UNIT_TYPEID::ZERG_LURKERMPBURROWED) < 6) {
				TryBuildUnit(ABILITY_ID::MORPH_LURKER, UNIT_TYPEID::ZERG_HYDRALISK);
			}
			if (larva_count > 0) {
				if (CountUnitType(observation, UNIT_TYPEID::ZERG_HYDRALISKDEN) + CountUnitType(observation, UNIT_TYPEID::ZERG_LURKERDENMP) > 0 && hydralisk_count < 15) {
					if (TryBuildUnit(ABILITY_ID::TRAIN_HYDRALISK, UNIT_TYPEID::ZERG_LARVA)) {
						--larva_count;
					}
				}
			}

			if (larva_count > 0) {
				if (swarmhost_count < 1) {
					if (TryBuildUnit(ABILITY_ID::TRAIN_SWARMHOST, UNIT_TYPEID::ZERG_LARVA)) {
						--larva_count;
					}
				}
			}

			if (larva_count > 0) {
				if (infestor_count < 1) {
					if (TryBuildUnit(ABILITY_ID::TRAIN_INFESTOR, UNIT_TYPEID::ZERG_LARVA)) {
						--larva_count;
					}
				}
			}
		}
		else {
			if (larva_count > 0) {
				if (CountUnitType(observation, UNIT_TYPEID::ZERG_SPIRE) + CountUnitType(observation, UNIT_TYPEID::ZERG_GREATERSPIRE) > 0) {
					if (corruptor_count < 7) {
						if (TryBuildUnit(ABILITY_ID::TRAIN_CORRUPTOR, UNIT_TYPEID::ZERG_LARVA)) {
							--larva_count;
						}
					}
					if (TryBuildUnit(ABILITY_ID::TRAIN_MUTALISK, UNIT_TYPEID::ZERG_LARVA)) {
						--larva_count;
					}
				}
			}
		}

		if (larva_count > 0) {
			if (++viper_count < 1) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_VIPER, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
			}
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_ULTRALISKCAVERN) > 0 && !mutalisk_build_ && ultralisk_count < 4) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_ULTRALISK, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
				//Try to build at least one ultralisk
				if (ultralisk_count < 1) {
					return;
				}
			}
		}
		if (CountUnitType(observation, UNIT_TYPEID::ZERG_GREATERSPIRE) > 0) {
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_BROODLORD) + CountUnitType(observation, UNIT_TYPEID::ZERG_BROODLORDCOCOON) < 4)
				TryBuildUnit(ABILITY_ID::MORPH_BROODLORD, UNIT_TYPEID::ZERG_CORRUPTOR);
		}

		if (!mutalisk_build_ && larva_count > 0) {
			if (roach_count < 10 && CountUnitType(observation, UNIT_TYPEID::ZERG_ROACHWARREN) > 0) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_ROACH, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
			}
		}
		size_t baneling_count = CountUnitType(observation, UNIT_TYPEID::ZERG_BANELING) + CountUnitType(observation, UNIT_TYPEID::ZERG_BANELINGCOCOON);
		if (larva_count > 0) {
			if (CountUnitType(observation, UNIT_TYPEID::ZERG_ZERGLING) < 20 && CountUnitType(observation, UNIT_TYPEID::ZERG_SPAWNINGPOOL) > 0) {
				if (TryBuildUnit(ABILITY_ID::TRAIN_ZERGLING, UNIT_TYPEID::ZERG_LARVA)) {
					--larva_count;
				}
			}
		}

		size_t baneling_target = 5;
		if (mutalisk_build_) {
			baneling_target = baneling_target * 2;
		}
		if (baneling_count < baneling_target) {
			TryBuildUnit(ABILITY_ID::TRAIN_BANELING, UNIT_TYPEID::ZERG_ZERGLING);
		}

	}

	void ZergMultiplayerBot::ManageUpgrades() {
		const ObservationInterface* observation = Observation();
		auto upgrades = observation->GetUpgrades();
		size_t base_count = observation->GetUnits(Unit::Alliance::Self, IsTownHall()).size();
		bool hive_tech = CountUnitType(observation, UNIT_TYPEID::ZERG_HIVE) > 0;
		bool lair_tech = CountUnitType(observation, UNIT_TYPEID::ZERG_LAIR) > 0 || hive_tech;

		if (upgrades.empty()) {
			TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGMETABOLICBOOST, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
		}
		else {
			for (const auto& upgrade : upgrades) {
				if (mutalisk_build_) {
					if (upgrade == UPGRADE_ID::ZERGFLYERWEAPONSLEVEL1 && base_count > 3) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_GREATERSPIRE);
					}
					else if (upgrade == UPGRADE_ID::ZERGFLYERARMORSLEVEL1 && base_count > 3) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_GREATERSPIRE);
					}
					else if (upgrade == UPGRADE_ID::ZERGFLYERWEAPONSLEVEL2 && base_count > 4) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_GREATERSPIRE);
					}
					else if (upgrade == UPGRADE_ID::ZERGFLYERARMORSLEVEL2 && base_count > 4) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_GREATERSPIRE);
					}
					else {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERATTACK, UNIT_TYPEID::ZERG_GREATERSPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_SPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGFLYERARMOR, UNIT_TYPEID::ZERG_GREATERSPIRE);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGGROUNDARMOR, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGMELEEWEAPONS, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
					}
				}//Not Mutalisk build only
				else {
					if (upgrade == UPGRADE_ID::ZERGMISSILEWEAPONSLEVEL1 && base_count > 3) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGMISSILEWEAPONS, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
					}
					else if (upgrade == UPGRADE_ID::ZERGMISSILEWEAPONSLEVEL2 && base_count > 4) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGMISSILEWEAPONS, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
					}
					if (upgrade == UPGRADE_ID::ZERGGROUNDARMORSLEVEL1 && base_count > 3) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGGROUNDARMOR, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
					}
					else if (upgrade == UPGRADE_ID::ZERGGROUNDARMORSLEVEL2 && base_count > 4) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGGROUNDARMOR, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
					}

					if (hive_tech) {
						TryBuildUnit(ABILITY_ID::RESEARCH_CHITINOUSPLATING, UNIT_TYPEID::ZERG_ULTRALISKCAVERN);
					}

					if (lair_tech) {
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGMISSILEWEAPONS, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
						TryBuildUnit(ABILITY_ID::RESEARCH_ZERGGROUNDARMOR, UNIT_TYPEID::ZERG_EVOLUTIONCHAMBER);
						TryBuildUnit(ABILITY_ID::RESEARCH_CENTRIFUGALHOOKS, UNIT_TYPEID::ZERG_BANELINGNEST);
						TryBuildUnit(ABILITY_ID::RESEARCH_MUSCULARAUGMENTS, UNIT_TYPEID::ZERG_HYDRALISKDEN);
						TryBuildUnit(ABILITY_ID::RESEARCH_MUSCULARAUGMENTS, UNIT_TYPEID::ZERG_LURKERDENMP);
						TryBuildUnit(ABILITY_ID::RESEARCH_GLIALREGENERATION, UNIT_TYPEID::ZERG_ROACHWARREN);
					}
				}
				//research regardless of build
				if (hive_tech) {
					TryBuildUnit(ABILITY_ID::RESEARCH_ZERGLINGADRENALGLANDS, UNIT_TYPEID::ZERG_SPAWNINGPOOL);
				}
				else if (lair_tech) {
					TryBuildUnit(ABILITY_ID::RESEARCH_PNEUMATIZEDCARAPACE, UNIT_TYPEID::ZERG_HIVE);
					TryBuildUnit(ABILITY_ID::RESEARCH_PNEUMATIZEDCARAPACE, UNIT_TYPEID::ZERG_LAIR);
					TryBuildUnit(ABILITY_ID::RESEARCH_PNEUMATIZEDCARAPACE, UNIT_TYPEID::ZERG_HATCHERY);
				}
			}
		}
	}

	bool ZergMultiplayerBot::TryBuildOverlord() {
		const ObservationInterface* observation = Observation();
		size_t larva_count = CountUnitType(observation, UNIT_TYPEID::ZERG_LARVA);
		if (observation->GetFoodCap() == 200) {
			return false;
		}
		if (observation->GetFoodUsed() < observation->GetFoodCap() - 4) {
			return false;
		}
		if (observation->GetMinerals() < 100) {
			return false;
		}

		//Slow overlord development in the beginning
		if (observation->GetFoodUsed() < 30) {
			Units units = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::ZERG_EGG));
			for (const auto& unit : units) {
				if (unit.orders.empty()) {
					return false;
				}
				if (unit.orders.front().ability_id == ABILITY_ID::TRAIN_OVERLORD) {
					return false;
				}
			}
		}
		if (larva_count > 0) {
			return TryBuildUnit(ABILITY_ID::TRAIN_OVERLORD, UNIT_TYPEID::ZERG_LARVA);
		}
		return false;
	}

	void ZergMultiplayerBot::TryInjectLarva() {
		const ObservationInterface* observation = Observation();
		Units queens = observation->GetUnits(Unit::Alliance::Self, IsUnit(UNIT_TYPEID::ZERG_QUEEN));
		Units hatcheries = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

		//if we don't have queens or hatcheries don't do anything
		if (queens.empty() || hatcheries.empty())
			return;

		for (size_t i = 0; i < queens.size(); ++i) {
			for (size_t j = 0; j < hatcheries.size(); ++j) {

				//if hatchery isn't complete ignore it
				if (hatcheries.at(j).build_progress != 1) {
					continue;
				}
				else {

					//Inject larva and move onto next available queen
					if (i < queens.size()) {
						if (queens.at(i).energy >= 25 && queens.at(i).orders.empty()) {
							Actions()->UnitCommand(queens.at(i).tag, ABILITY_ID::EFFECT_INJECTLARVA, hatcheries.at(j).tag);
						}
						++i;
					}
				}
			}
		}
	}

	bool ZergMultiplayerBot::TryBuildExpansionHatch() {
		const ObservationInterface* observation = Observation();

		//Don't have more active bases than we can provide workers for
		if (GetExpectedWorkers(UNIT_TYPEID::ZERG_EXTRACTOR) > max_worker_count_) {
			return false;
		}
		// If we have extra workers around, try and build another Hatch.
		if (GetExpectedWorkers(UNIT_TYPEID::ZERG_EXTRACTOR) < observation->GetFoodWorkers() - 10) {
			return TryExpand(ABILITY_ID::BUILD_HATCHERY, UNIT_TYPEID::ZERG_DRONE);
		}
		//Only build another Hatch if we are floating extra minerals
		if (observation->GetMinerals() > std::min<size_t>((CountUnitType(observation, UNIT_TYPEID::ZERG_HATCHERY) * 300), 1200)) {
			return TryExpand(ABILITY_ID::BUILD_HATCHERY, UNIT_TYPEID::ZERG_DRONE);
		}
		return false;
	}

	bool ZergMultiplayerBot::BuildExtractor() {
		const ObservationInterface* observation = Observation();
		Units bases = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

		if (CountUnitType(observation, UNIT_TYPEID::ZERG_EXTRACTOR) >= observation->GetUnits(Unit::Alliance::Self, IsTownHall()).size() * 2) {
			return false;
		}
		
		for (const auto& base : bases) {
			if (base.assigned_harvesters >= base.ideal_harvesters) {
				if (base.build_progress == 1) {
					if (TryBuildGas(ABILITY_ID::BUILD_EXTRACTOR, UNIT_TYPEID::ZERG_DRONE, base.pos)) {
						return true;
					}
				}
			}
		}
		return false;
	}

	void ZergMultiplayerBot::OnStep() {

		const ObservationInterface* observation = Observation();
		Units base = observation->GetUnits(Unit::Alliance::Self, IsTownHall());

		//Throttle some behavior that can wait to avoid duplicate orders.
		int frames_to_skip = 4;
		if (observation->GetFoodUsed() >= observation->GetFoodCap()) {
			frames_to_skip = 6;
		}

		if (observation->GetGameLoop() % frames_to_skip != 0) {
			return;
		}

		if (!nuke_detected) {
			ManageArmy();
		}
		else {
			if (nuke_detected_frame + 400 < observation->GetGameLoop()) {
				nuke_detected = false;
			}
			Units units = observation->GetUnits(Unit::Self, IsArmy(observation));
			for (const auto& unit : units) {
				RetreatWithUnit(unit, startLocation_);
			}
		}

		BuildOrder();

		TryInjectLarva();

		ManageWorkers(UNIT_TYPEID::ZERG_DRONE, ABILITY_ID::HARVEST_GATHER, UNIT_TYPEID::ZERG_EXTRACTOR);

		ManageUpgrades();

		if (TryBuildDrone())
			return;

		if (TryBuildOverlord())
			return;

		if (observation->GetFoodArmy() < base.size() * 25) {
			BuildArmy();
		}

		if (BuildExtractor()) {
			return;
		}

		if (TryBuildExpansionHatch()) {
			return;
		}
	}

	void ZergMultiplayerBot::OnUnitIdle(const Unit& unit) {
		switch (unit.unit_type.ToType()) {
		case UNIT_TYPEID::ZERG_DRONE: {
										  MineIdleWorkers(unit.tag, ABILITY_ID::HARVEST_GATHER, UNIT_TYPEID::ZERG_EXTRACTOR);
										  break;
		}
		default:
			break;
		}
	}


	int main(int argc, char* argv[]) {
		Coordinator coordinator;
		coordinator.LoadSettings(argc, argv);

		Bot bot;
		coordinator.SetParticipants({
			CreateParticipant(Race::Zerg, &bot),
			CreateComputer(Race::Zerg)
		});

		coordinator.LaunchStarcraft();
		coordinator.StartGame(sc2::kMapBelShirVestigeLE);

		while (coordinator.Update()) {
		}

		return 0;
	}
}