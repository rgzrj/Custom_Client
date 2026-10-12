#include "protocol/rm2026/state_mapping.hpp"
#ifdef RM27_UPSTREAM_BACKEND
#include "robomaster.pb.h"
#else
#include "client.pb.h"
#endif
#include <QJsonArray>
#include <QByteArray>

namespace rm27::protocol::rm2026 {
namespace domain = historical::rm2026;
namespace {
domain::OutpostLife outpostLife(const domain::Structure& outpost)
{
    // PDF 53-54: 0..2 invincible/alive, 3..5 destroyed/rebuilding.
    if (outpost.status && *outpost.status > 5) return domain::OutpostLife::Unknown;
    if (outpost.status && outpost.health &&
        ((*outpost.status <= 2) != (*outpost.health > 0))) return domain::OutpostLife::Unknown;
    if (outpost.status) return *outpost.status <= 2 ? domain::OutpostLife::Alive : domain::OutpostLife::Destroyed;
    if (outpost.health) return *outpost.health > 0 ? domain::OutpostLife::Alive : domain::OutpostLife::Destroyed;
    return domain::OutpostLife::Unknown;
}

template<class T> void put(QJsonObject& json, const char* key, const std::optional<T>& value)
{
    if (value) json.insert(key, double(*value));
}
domain::Module module(std::uint32_t code)
{
    return {code, code <= 2 ? static_cast<domain::ModuleState>(code) : domain::ModuleState::Unknown};
}
void putModule(QJsonObject& json, const char* key, const domain::Module& value)
{
    if (!value.code) return;
    constexpr std::array states{"offline", "online", "installation_offline"};
    const int state = static_cast<int>(value.state);
    json.insert(key, QJsonObject{{"code", double(*value.code)},
        {"state", state >= 0 && state < int(states.size()) ? states[state] : "unknown"}});
}
QJsonObject structureJson(const domain::Structure& structure, bool outpost)
{
    QJsonObject json;
    put(json, "health", structure.health);
    put(json, "status", structure.status);
    put(json, "shield", structure.shield);
    if (outpost) json.insert("life", structure.life == domain::OutpostLife::Alive ? "alive" :
        structure.life == domain::OutpostLife::Destroyed ? "destroyed" : "unknown");
    return json;
}
QJsonObject sideJson(const domain::SideUnits& side)
{
    QJsonArray robots;
    for (const auto& robot : side.robots) {
        QJsonObject json{{"role_id", int(robot.roleId)}};
        put(json, "health", robot.health);
        robots.append(json);
    }
    return {{"base", structureJson(side.base, false)},
        {"outpost", structureJson(side.outpost, true)}, {"robots", robots}};
}
} // namespace

domain::SelfConfig mapConfig(const wire::RobotStaticStatus& message)
{
    domain::SelfConfig value;
    if (message.has_robot_id()) {
        value.robotId = message.robot_id();
        if (message.robot_id() >= 1 && message.robot_id() <= 7) value.team = domain::Team::Red;
        if (message.robot_id() >= 101 && message.robot_id() <= 107) value.team = domain::Team::Blue;
    }
    if (message.has_connection_state()) value.connectionState = message.connection_state();
    if (message.has_field_state()) value.fieldState = message.field_state();
    if (message.has_alive_state()) value.aliveState = message.alive_state();
    if (message.has_performance_system_shooter()) value.shooter = message.performance_system_shooter();
    if (message.has_performance_system_chassis()) value.chassis = message.performance_system_chassis();
    if (message.has_max_health()) value.maxHealth = message.max_health();
    if (message.has_level()) value.level = message.level();
    if (message.has_robot_type()) value.robotType = message.robot_type();
    if (message.has_max_heat()) value.maxHeat = message.max_heat();
    if (message.has_heat_cooldown_rate()) value.heatCooldownRate = message.heat_cooldown_rate();
    if (message.has_max_power()) value.maxPower = message.max_power();
    if (message.has_max_buffer_energy()) value.maxBufferEnergy = message.max_buffer_energy();
    if (message.has_max_chassis_energy()) value.maxChassisEnergy = message.max_chassis_energy();
    return value;
}

domain::SelfDynamic mapDynamic(const wire::RobotDynamicStatus& message)
{
    domain::SelfDynamic value;
    if (message.has_is_out_of_combat()) value.outOfCombat = message.is_out_of_combat();
    if (message.has_can_remote_heal()) value.canRemoteHeal = message.can_remote_heal();
    if (message.has_can_remote_ammo()) value.canRemoteAmmo = message.can_remote_ammo();
    if (message.has_current_health()) value.health = message.current_health();
    if (message.has_remaining_ammo()) value.permittedShots = message.remaining_ammo();
    if (message.has_current_heat()) value.heat = message.current_heat();
    if (message.has_last_projectile_fire_rate()) value.lastProjectileFireRate = message.last_projectile_fire_rate();
    if (message.has_current_chassis_energy()) value.chassisEnergy = message.current_chassis_energy();
    if (message.has_current_buffer_energy()) value.bufferEnergy = message.current_buffer_energy();
    if (message.has_current_experience()) value.experience = message.current_experience();
    if (message.has_experience_for_upgrade()) value.experienceForUpgrade = message.experience_for_upgrade();
    if (message.has_total_projectiles_fired()) value.totalProjectilesFired = message.total_projectiles_fired();
    if (message.has_out_of_combat_countdown()) value.outOfCombatCountdown = message.out_of_combat_countdown();
    return value;
}

domain::Units mapUnits(const wire::GlobalUnitStatus& message)
{
    domain::Units value;
    if (message.has_base_health()) value.ally.base.health = message.base_health();
    if (message.has_base_status()) value.ally.base.status = message.base_status();
    if (message.has_base_shield()) value.ally.base.shield = message.base_shield();
    if (message.has_outpost_health()) value.ally.outpost.health = message.outpost_health();
    if (message.has_outpost_status()) value.ally.outpost.status = message.outpost_status();
    if (message.has_enemy_base_health()) value.enemy.base.health = message.enemy_base_health();
    if (message.has_enemy_base_status()) value.enemy.base.status = message.enemy_base_status();
    if (message.has_enemy_base_shield()) value.enemy.base.shield = message.enemy_base_shield();
    if (message.has_enemy_outpost_health()) value.enemy.outpost.health = message.enemy_outpost_health();
    if (message.has_enemy_outpost_status()) value.enemy.outpost.status = message.enemy_outpost_status();
    value.ally.outpost.life = outpostLife(value.ally.outpost);
    value.enemy.outpost.life = outpostLife(value.enemy.outpost);
    if (message.robot_health_size() == 10) {
        for (int index = 0; index < 5; ++index) {
            value.ally.robots[index].health = message.robot_health(index);
            value.enemy.robots[index].health = message.robot_health(index + 5);
        }
    }
    value.robotBullets.assign(message.robot_bullets().begin(), message.robot_bullets().end());
    if (message.has_total_damage_ally()) value.totalDamageAlly = message.total_damage_ally();
    if (message.has_total_damage_enemy()) value.totalDamageEnemy = message.total_damage_enemy();
    return value;
}

domain::Economy mapEconomy(const wire::GlobalLogisticsStatus& message)
{
    domain::Economy value;
    if (message.has_remaining_economy()) value.remaining = message.remaining_economy();
    if (message.has_total_economy_obtained()) value.totalObtained = message.total_economy_obtained();
    if (message.has_tech_level()) value.techLevel = message.tech_level();
    if (message.has_encryption_level()) value.encryptionLevel = message.encryption_level();
    return value;
}

domain::Game mapGame(const wire::GameStatus& message)
{
    domain::Game value;
    if (message.has_current_round()) value.currentRound = message.current_round();
    if (message.has_total_rounds()) value.totalRounds = message.total_rounds();
    if (message.has_red_score()) value.redScore = message.red_score();
    if (message.has_blue_score()) value.blueScore = message.blue_score();
    if (message.has_current_stage()) {
        value.stageCode = message.current_stage();
        if (message.current_stage() <= 5) value.phase = static_cast<domain::Phase>(message.current_stage());
    }
    if (message.has_stage_countdown_sec()) value.countdownSec = message.stage_countdown_sec();
    if (message.has_stage_elapsed_sec()) value.elapsedSec = message.stage_elapsed_sec();
    if (message.has_is_paused()) value.paused = message.is_paused();
    if (message.has_game_result()) value.reportedResult = message.game_result();
    if (message.has_end_reason()) value.reportedEndReason = message.end_reason();
    if (value.phase == domain::Phase::Settlement) {
        if (value.reportedResult && *value.reportedResult <= 2) value.result = value.reportedResult;
        if (value.reportedEndReason && *value.reportedEndReason >= 1 && *value.reportedEndReason <= 9)
            value.endReason = value.reportedEndReason;
    }
    return value;
}

domain::SpecialMechanisms mapSpecialMechanisms(const wire::GlobalSpecialMechanism& message)
{
    domain::SpecialMechanisms value;
    value.entries.reserve(message.mechanism_id_size());
    // Ingress has validated equal lengths. Do not truncate, clamp or merge these pairs.
    for (int i = 0; i < message.mechanism_id_size(); ++i) {
        const auto id = message.mechanism_id(i);
        value.entries.push_back({id, message.mechanism_time_sec(i),
            id == 1 ? domain::MechanismKind::AllyFortressOccupied :
                id == 2 ? domain::MechanismKind::EnemyFortressOccupied : domain::MechanismKind::Unknown});
    }
    return value;
}

domain::InjuryStat mapInjury(const wire::RobotInjuryStat& message)
{
    domain::InjuryStat value;
    if (message.has_total_damage()) value.totalDamage = message.total_damage();
    if (message.has_collision_damage()) value.collisionDamage = message.collision_damage();
    if (message.has_small_projectile_damage()) value.smallProjectileDamage = message.small_projectile_damage();
    if (message.has_large_projectile_damage()) value.largeProjectileDamage = message.large_projectile_damage();
    if (message.has_dart_splash_damage()) value.dartSplashDamage = message.dart_splash_damage();
    if (message.has_module_offline_damage()) value.moduleOfflineDamage = message.module_offline_damage();
    if (message.has_offline_damage()) value.offlineDamage = message.offline_damage();
    if (message.has_penalty_damage()) value.penaltyDamage = message.penalty_damage();
    if (message.has_server_kill_damage()) value.serverKillDamage = message.server_kill_damage();
    if (message.has_killer_id()) value.killerId = message.killer_id();
    return value;
}

domain::PathPlan mapPathPlan(const wire::RobotPathPlanInfo& message)
{
    domain::PathPlan value;
    if (message.has_intention()) value.intention = message.intention();
    if (message.has_start_pos_x()) value.startXdm = message.start_pos_x();
    if (message.has_start_pos_y()) value.startYdm = message.start_pos_y();
    if (message.has_sender_id()) value.senderId = message.sender_id();
    value.offsets.reserve(message.offset_x_size());
    // Ingress validates paired lengths/ranges. Preserve increments without normalization.
    for (int i = 0; i < message.offset_x_size(); ++i)
        value.offsets.push_back({message.offset_x(i), message.offset_y(i)});
    return value;
}

domain::MapClickReport mapMapClick(const wire::MapClickInfo& message)
{
    domain::MapClickReport value;
    if (message.has_is_send_all()) value.sendAll = message.is_send_all();
    if (message.has_robot_id()) {
        value.robotIds.emplace(); // Ingress has validated exactly seven bytes.
        for (size_t i = 0; i < value.robotIds->size(); ++i)
            (*value.robotIds)[i] = static_cast<std::uint8_t>(message.robot_id()[i]);
    }
    if (message.has_mode()) value.markingType = message.mode();
    if (message.has_enemy_id()) value.enemyId = message.enemy_id();
    if (message.has_ascii()) value.ascii = message.ascii();
    if (message.has_type()) value.markingMode = message.type();
    if (message.has_map_x()) value.x = message.map_x();
    if (message.has_map_y()) value.y = message.map_y();
    return value;
}

domain::CustomBytes mapCustomBytes(const wire::CustomByteBlock& message)
{
    domain::CustomBytes value;
    if (message.has_data()) value.data.emplace(message.data().begin(), message.data().end());
    return value;
}

domain::Penalty mapPenalty(const wire::PenaltyInfo& message)
{
    domain::Penalty value;
    if (message.has_penalty_type()) {
        value.typeCode = message.penalty_type();
        if (message.penalty_type() >= 1 && message.penalty_type() <= 6)
            value.kind = static_cast<domain::PenaltyKind>(message.penalty_type());
    }
    if (message.has_penalty_effect_sec()) value.effectSec = message.penalty_effect_sec();
    if (message.has_total_penalty_num()) value.total = message.total_penalty_num();
    return value;
}

domain::Modules mapModules(const wire::RobotModuleStatus& message)
{
    domain::Modules value;
    // PDF 60-61: all eleven modules share codes 0/1/2, not bool/nonzero.
    if (message.has_power_manager()) value.powerManager = module(message.power_manager());
    if (message.has_rfid()) value.rfid = module(message.rfid());
    if (message.has_light_strip()) value.lightStrip = module(message.light_strip());
    if (message.has_small_shooter()) value.smallShooter = module(message.small_shooter());
    if (message.has_big_shooter()) value.bigShooter = module(message.big_shooter());
    if (message.has_uwb()) value.uwb = module(message.uwb());
    if (message.has_armor()) value.armor = module(message.armor());
    if (message.has_video_transmission()) value.videoTransmission = module(message.video_transmission());
    if (message.has_capacitor()) value.capacitor = module(message.capacitor());
    if (message.has_main_controller()) value.mainController = module(message.main_controller());
    if (message.has_laser_detection_module()) value.laserDetectionModule = module(message.laser_detection_module());
    return value;
}

domain::Respawn mapRespawn(const wire::RobotRespawnStatus& message)
{
    domain::Respawn value;
    if (message.has_is_pending_respawn()) value.pending = message.is_pending_respawn();
    if (message.has_total_respawn_progress()) value.requiredProgress = message.total_respawn_progress();
    if (message.has_current_respawn_progress()) value.currentProgress = message.current_respawn_progress();
    if (message.has_can_free_respawn()) value.reportedCanFree = message.can_free_respawn();
    if (message.has_gold_cost_for_respawn()) value.goldCost = message.gold_cost_for_respawn();
    if (message.has_can_pay_for_respawn()) value.reportedCanPay = message.can_pay_for_respawn();
    return value;
}

domain::EventReport mapEvent(const wire::Event& message)
{
    domain::EventReport value;
    if (message.has_event_id()) {
        value.id = message.event_id();
        if (message.event_id() >= 1 && message.event_id() <= 15)
            value.kind = static_cast<domain::EventKind>(message.event_id());
    }
    if (message.has_param()) value.param = message.param();
    return value;
}

domain::Position mapPosition(const wire::RobotPosition& message)
{
    domain::Position value;
    if (message.has_x()) value.x = message.x();
    if (message.has_y()) value.y = message.y();
    if (message.has_z()) value.z = message.z();
    if (message.has_yaw()) value.yawDeg = message.yaw();
    if (message.has_robot_id()) value.reportedRobotId = message.robot_id();
    return value;
}

domain::BuffReport mapBuff(const wire::Buff& message)
{
    domain::BuffReport value;
    if (message.has_robot_id()) value.reportedRobotId = message.robot_id();
    if (message.has_buff_type()) {
        value.typeCode = message.buff_type();
        if (message.buff_type() >= 1 && message.buff_type() <= 7)
            value.kind = static_cast<domain::BuffKind>(message.buff_type());
    }
    if (message.has_buff_level()) value.level = message.buff_level();
    if (message.has_buff_max_time()) value.maxTime = message.buff_max_time();
    if (message.has_buff_left_time()) value.leftTime = message.buff_left_time();
    return value;
}

domain::Performance mapPerformance(const wire::RobotPerformanceSelectionSync& message)
{
    domain::Performance value;
    if (message.has_shooter()) value.shooter = message.shooter();
    if (message.has_chassis()) value.chassis = message.chassis();
    if (message.has_sentry_control()) value.sentryControl = message.sentry_control();
    return value;
}

domain::Deployment mapDeployment(const wire::DeployModeStatusSync& message)
{
    domain::Deployment value;
    if (message.has_status()) {
        value.code = message.status();
        if (message.status() <= 1) value.state = static_cast<domain::DeployState>(message.status());
    }
    return value;
}

domain::TechCore mapTechCore(const wire::TechCoreMotionStateSync& message)
{
    domain::TechCore value;
    if (message.has_maximum_difficulty_level()) value.maximumDifficulty = message.maximum_difficulty_level();
    if (message.has_basic_state()) value.basicState = message.basic_state();
    if (message.has_putin_state()) value.putIn = message.putin_state();
    if (message.has_move_state()) value.move = message.move_state();
    if (message.has_rotate_state()) value.rotate = message.rotate_state();
    if (message.has_enemy_core_status()) value.enemyCoreStatus = message.enemy_core_status();
    if (message.has_remain_time_all()) value.remainTimeAll = message.remain_time_all();
    if (message.has_remain_time_step()) value.remainTimeStep = message.remain_time_step();
    return value;
}

domain::RuneStatus mapRune(const wire::RuneStatusSync& message)
{
    domain::RuneStatus value;
    if (message.has_rune_status()) value.code = message.rune_status();
    if (message.has_activated_arms()) value.activatedArms = message.activated_arms();
    if (message.has_average_rings()) value.averageRings = message.average_rings();
    return value;
}

domain::SentryStatus mapSentryStatus(const wire::SentryStatusSync& message)
{
    domain::SentryStatus value;
    if (message.has_posture_id()) {
        value.postureId = message.posture_id();
        if (*value.postureId >= 1 && *value.postureId <= 3)
            value.posture = static_cast<domain::SentryPosture>(*value.postureId);
    }
    if (message.has_is_weakened()) value.weakened = message.is_weakened();
    if (message.has_is_powered()) value.powered = message.is_powered();
    return value;
}

domain::DartStatus mapDartStatus(const wire::DartSelectTargetStatusSync& message)
{
    domain::DartStatus value;
    if (message.has_target_id()) {
        value.targetId = message.target_id();
        if (*value.targetId >= 1 && *value.targetId <= 5)
            value.target = static_cast<domain::DartTarget>(*value.targetId);
    }
    if (message.has_open()) {
        value.gateCode = message.open();
        if (*value.gateCode <= 2) value.gate = static_cast<domain::DartGate>(*value.gateCode);
    }
    return value;
}

domain::AirSupport mapAirSupport(const wire::AirSupportStatusSync& message)
{
    domain::AirSupport value;
    if (message.has_airsupport_status()) value.status = message.airsupport_status();
    if (message.has_left_time()) value.freeTime = message.left_time();
    if (message.has_cost_coins()) value.costCoins = message.cost_coins();
    if (message.has_is_being_targeted()) value.targeted = message.is_being_targeted();
    if (message.has_shooter_status()) value.shooterStatus = message.shooter_status();
    return value;
}

domain::SentryResult mapSentryResult(const wire::SentryCtrlResult& message)
{
    domain::SentryResult value;
    if (message.has_command_id()) value.commandId = message.command_id();
    if (message.has_result_code()) value.resultCode = message.result_code();
    return value;
}

QJsonObject snapshotJson(const domain::Snapshot& snapshot)
{
    // This is a local constraint, not a PenaltyInfo field or release acknowledgement.
    QJsonObject json{{"control_constraints", QJsonObject{
        {"penalty_release_unverified", snapshot.penaltyReleaseUnverified}}}};
    if (snapshot.selfConfig) {
        const auto& value = snapshot.selfConfig->value;
        QJsonObject config{{"team", value.team == domain::Team::Red ? "red" :
            value.team == domain::Team::Blue ? "blue" : "unknown"}};
        put(config, "robot_id", value.robotId);
        put(config, "max_health", value.maxHealth);
        put(config, "level", value.level);
        put(config, "connection_state", value.connectionState);
        put(config, "field_state", value.fieldState);
        put(config, "alive_state", value.aliveState);
        put(config, "shooter", value.shooter);
        put(config, "chassis", value.chassis);
        put(config, "robot_type", value.robotType);
        put(config, "max_heat", value.maxHeat);
        put(config, "heat_cooldown_rate", value.heatCooldownRate);
        put(config, "max_power", value.maxPower);
        put(config, "max_buffer_energy", value.maxBufferEnergy);
        put(config, "max_chassis_energy", value.maxChassisEnergy);
        json.insert("self_config", config);
    }
    if (snapshot.selfDynamic) {
        const auto& value = snapshot.selfDynamic->value;
        QJsonObject dynamic;
        put(dynamic, "health", value.health);
        put(dynamic, "permitted_shots", value.permittedShots);
        put(dynamic, "heat", value.heat);
        put(dynamic, "last_projectile_fire_rate", value.lastProjectileFireRate);
        put(dynamic, "chassis_energy", value.chassisEnergy);
        put(dynamic, "buffer_energy", value.bufferEnergy);
        put(dynamic, "experience", value.experience);
        put(dynamic, "experience_for_upgrade", value.experienceForUpgrade);
        put(dynamic, "total_projectiles_fired", value.totalProjectilesFired);
        put(dynamic, "out_of_combat_countdown", value.outOfCombatCountdown);
        if (value.outOfCombat) dynamic.insert("out_of_combat", *value.outOfCombat);
        if (value.canRemoteHeal) dynamic.insert("can_remote_heal", *value.canRemoteHeal);
        if (value.canRemoteAmmo) dynamic.insert("can_remote_ammo", *value.canRemoteAmmo);
        json.insert("self_dynamic", dynamic);
    }
    if (snapshot.units) {
        const auto& value = snapshot.units->value;
        QJsonObject units{{"ally", sideJson(value.ally)}, {"enemy", sideJson(value.enemy)}};
        if (!value.robotBullets.empty()) {
            QJsonArray bullets;
            for (const auto amount : value.robotBullets) bullets.append(amount);
            units.insert("robot_bullets", bullets);
        }
        put(units, "total_damage_ally", value.totalDamageAlly);
        put(units, "total_damage_enemy", value.totalDamageEnemy);
        json.insert("units", units);
    }
    if (snapshot.economy) {
        QJsonObject economy;
        put(economy, "remaining", snapshot.economy->value.remaining);
        if (snapshot.economy->value.totalObtained)
            economy.insert("total_obtained", QString::number(*snapshot.economy->value.totalObtained));
        put(economy, "tech_level", snapshot.economy->value.techLevel);
        put(economy, "encryption_level", snapshot.economy->value.encryptionLevel);
        json.insert("economy", economy);
    }
    if (snapshot.specialMechanisms) {
        QJsonArray entries;
        for (const auto& value : snapshot.specialMechanisms->value.entries) {
            entries.append(QJsonObject{{"id", double(value.id)}, {"time_sec", value.timeSec},
                {"kind", value.kind == domain::MechanismKind::AllyFortressOccupied ? "ally_fortress_occupied" :
                    value.kind == domain::MechanismKind::EnemyFortressOccupied ? "enemy_fortress_occupied" : "unknown"}});
        }
        json.insert("special_mechanisms", QJsonObject{{"entries", entries}});
    }
    if (snapshot.injury) {
        const auto& value = snapshot.injury->value;
        QJsonObject injury;
        put(injury, "total_damage", value.totalDamage);
        put(injury, "collision_damage", value.collisionDamage);
        put(injury, "small_projectile_damage", value.smallProjectileDamage);
        put(injury, "large_projectile_damage", value.largeProjectileDamage);
        put(injury, "dart_splash_damage", value.dartSplashDamage);
        put(injury, "module_offline_damage", value.moduleOfflineDamage);
        put(injury, "offline_damage", value.offlineDamage);
        put(injury, "penalty_damage", value.penaltyDamage);
        put(injury, "server_kill_damage", value.serverKillDamage);
        put(injury, "killer_id", value.killerId);
        json.insert("injury", injury);
    }
    if (snapshot.pathPlan) {
        const auto& value = snapshot.pathPlan->value;
        QJsonObject path;
        put(path, "intention", value.intention);
        put(path, "start_x_dm", value.startXdm);
        put(path, "start_y_dm", value.startYdm);
        put(path, "sender_id", value.senderId);
        QJsonArray offsets;
        for (const auto& offset : value.offsets)
            offsets.append(QJsonObject{{"x", offset.x}, {"y", offset.y}});
        path.insert("offsets", offsets);
        json.insert("path_plan", path);
    }
    if (snapshot.mapClick) {
        const auto& value = snapshot.mapClick->value;
        QJsonObject map;
        put(map, "send_all", value.sendAll);
        if (value.robotIds) {
            QJsonArray ids;
            for (const auto id : *value.robotIds) ids.append(int(id));
            map.insert("robot_ids", ids);
        }
        put(map, "marking_type", value.markingType);
        put(map, "enemy_id", value.enemyId);
        put(map, "ascii", value.ascii);
        put(map, "marking_mode", value.markingMode);
        put(map, "x", value.x);
        put(map, "y", value.y);
        json.insert("map_click_report", map);
    }
    if (snapshot.customBytes) {
        QJsonObject bytes;
        if (const auto& data = snapshot.customBytes->value.data; data)
            bytes.insert("data_hex", QString::fromLatin1(QByteArray(
                reinterpret_cast<const char*>(data->data()), qsizetype(data->size())).toHex()));
        json.insert("custom_bytes", bytes);
    }
    if (snapshot.game) {
        const auto& value = snapshot.game->value;
        constexpr std::array phases{"not_started", "preparation", "self_check", "countdown", "playing", "settlement"};
        const int phase = static_cast<int>(value.phase);
        QJsonObject game{{"phase", phase >= 0 && phase < int(phases.size()) ? phases[phase] : "unknown"}};
        put(game, "current_round", value.currentRound);
        put(game, "total_rounds", value.totalRounds);
        put(game, "red_score", value.redScore);
        put(game, "blue_score", value.blueScore);
        put(game, "stage_code", value.stageCode);
        put(game, "countdown_sec", value.countdownSec);
        put(game, "elapsed_sec", value.elapsedSec);
        if (value.paused) game.insert("paused", *value.paused);
        put(game, "reported_result", value.reportedResult);
        put(game, "reported_end_reason", value.reportedEndReason);
        put(game, "result", value.result);
        put(game, "end_reason", value.endReason);
        json.insert("game", game);
    }
    if (snapshot.penalty) {
        const auto& value = snapshot.penalty->value;
        constexpr std::array kinds{"unknown", "yellow_card", "both_yellow_cards", "red_card", "power", "heat", "fire_rate"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject penalty{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(penalty, "type_code", value.typeCode);
        put(penalty, "effect_sec", value.effectSec);
        put(penalty, "total", value.total);
        json.insert("penalty", penalty);
    }
    if (snapshot.modules) {
        const auto& value = snapshot.modules->value;
        QJsonObject modules;
        putModule(modules, "power_manager", value.powerManager);
        putModule(modules, "rfid", value.rfid);
        putModule(modules, "light_strip", value.lightStrip);
        putModule(modules, "small_shooter", value.smallShooter);
        putModule(modules, "big_shooter", value.bigShooter);
        putModule(modules, "uwb", value.uwb);
        putModule(modules, "armor", value.armor);
        putModule(modules, "video_transmission", value.videoTransmission);
        putModule(modules, "capacitor", value.capacitor);
        putModule(modules, "main_controller", value.mainController);
        putModule(modules, "laser_detection_module", value.laserDetectionModule);
        json.insert("modules", modules);
    }
    if (snapshot.respawn) {
        const auto& value = snapshot.respawn->value;
        QJsonObject respawn;
        if (value.pending) respawn.insert("pending", *value.pending);
        if (value.reportedCanFree) respawn.insert("reported_can_free", *value.reportedCanFree);
        if (value.reportedCanPay) respawn.insert("reported_can_pay", *value.reportedCanPay);
        put(respawn, "required_progress", value.requiredProgress);
        put(respawn, "current_progress", value.currentProgress);
        put(respawn, "gold_cost", value.goldCost);
        json.insert("respawn", respawn);
    }
    if (snapshot.latestEvent) {
        const auto& value = snapshot.latestEvent->value;
        constexpr std::array kinds{"unknown", "kill", "outpost_destroyed", "large_energy_result", "energy_activated",
            "ally_sniper_damage", "enemy_sniper_damage", "enemy_air_support", "enemy_air_support_countered",
            "dart_hit", "enemy_dart_gate_opened", "base_attacked", "enemy_outpost_stopped",
            "enemy_base_armor_opened", "enemy_assembly_forced_exit", "assembly_result"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject event{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(event, "id", value.id);
        if (value.param) event.insert("param", QString::fromUtf8(value.param->data(), qsizetype(value.param->size())));
        json.insert("latest_event", event);
    }
    if (snapshot.position) {
        const auto& value = snapshot.position->value;
        QJsonObject position;
        put(position, "x", value.x);
        put(position, "y", value.y);
        put(position, "z", value.z);
        put(position, "yaw_deg", value.yawDeg);
        put(position, "reported_robot_id", value.reportedRobotId);
        json.insert("position", position);
    }
    if (snapshot.latestBuff) {
        const auto& value = snapshot.latestBuff->value;
        constexpr std::array kinds{"unknown", "attack", "defense_or_vulnerability", "cooling",
            "chassis_power", "recovery", "exchangeable_shots", "terrain_prewarning"};
        const int kind = static_cast<int>(value.kind);
        QJsonObject buff{{"kind", kind >= 0 && kind < int(kinds.size()) ? kinds[kind] : "unknown"}};
        put(buff, "reported_robot_id", value.reportedRobotId);
        put(buff, "type_code", value.typeCode);
        put(buff, "level", value.level);
        put(buff, "max_time", value.maxTime);
        put(buff, "left_time", value.leftTime);
        json.insert("latest_buff", buff);
    }
    if (snapshot.performance) {
        const auto& value = snapshot.performance->value;
        QJsonObject performance;
        put(performance, "shooter", value.shooter);
        put(performance, "chassis", value.chassis);
        put(performance, "sentry_control", value.sentryControl);
        json.insert("performance", performance);
    }
    if (snapshot.deployment) {
        const auto& value = snapshot.deployment->value;
        QJsonObject deployment{{"state", value.state == domain::DeployState::NotDeployed ? "not_deployed" :
            value.state == domain::DeployState::Deployed ? "deployed" : "unknown"}};
        put(deployment, "code", value.code);
        json.insert("deployment", deployment);
    }
    if (snapshot.techCore) {
        const auto& value = snapshot.techCore->value;
        QJsonObject core;
        put(core, "maximum_difficulty", value.maximumDifficulty);
        put(core, "basic_state", value.basicState);
        put(core, "put_in", value.putIn);
        put(core, "move", value.move);
        put(core, "rotate", value.rotate);
        put(core, "enemy_core_status", value.enemyCoreStatus);
        put(core, "remain_time_all", value.remainTimeAll);
        put(core, "remain_time_step", value.remainTimeStep);
        json.insert("tech_core", core);
    }
    if (snapshot.rune) {
        QJsonObject rune;
        put(rune, "code", snapshot.rune->value.code);
        put(rune, "activated_arms", snapshot.rune->value.activatedArms);
        put(rune, "average_rings", snapshot.rune->value.averageRings);
        json.insert("rune", rune);
    }
    if (snapshot.sentryStatus) {
        const auto& value = snapshot.sentryStatus->value;
        constexpr std::array postures{"unknown", "attack", "defense", "move"};
        QJsonObject sentry{{"posture", postures[static_cast<int>(value.posture)]}};
        put(sentry, "posture_id", value.postureId);
        if (value.weakened) sentry.insert("weakened", *value.weakened);
        if (value.powered) sentry.insert("powered", *value.powered);
        json.insert("sentry_status", sentry);
    }
    if (snapshot.dartStatus) {
        const auto& value = snapshot.dartStatus->value;
        constexpr std::array targets{"unknown", "outpost", "base_fixed", "base_random_fixed",
            "base_random_moving", "base_end_moving"};
        QJsonObject dart{{"target", targets[static_cast<int>(value.target)]},
            {"gate", value.gate == domain::DartGate::Closed ? "closed" :
                value.gate == domain::DartGate::Opening ? "opening" :
                value.gate == domain::DartGate::Open ? "open" : "unknown"}};
        put(dart, "target_id", value.targetId);
        put(dart, "gate_code", value.gateCode);
        json.insert("dart_status", dart);
    }
    if (snapshot.airSupport) {
        const auto& value = snapshot.airSupport->value;
        QJsonObject air;
        put(air, "status", value.status);
        put(air, "free_time", value.freeTime);
        put(air, "cost_coins", value.costCoins);
        put(air, "targeted", value.targeted);
        put(air, "shooter_status", value.shooterStatus);
        json.insert("air_support", air);
    }
    if (snapshot.sentryResult) {
        QJsonObject result;
        put(result, "command_id", snapshot.sentryResult->value.commandId);
        put(result, "result_code", snapshot.sentryResult->value.resultCode);
        // This report has no unique request identifier; it is not a local command ACK.
        json.insert("sentry_result_report", result);
    }
    return json;
}
} // namespace rm27::protocol::rm2026
