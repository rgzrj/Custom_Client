#include "sim/sim_referee_receiver.hpp"

#include <cassert>
#include <iostream>

int main()
{
    const QByteArray valid = R"json({
        "channel":"rm27.sim.v1.referee",
        "sequence":7,
        "sent_at_unix_ms":1789999999000,
        "phase":"match",
        "telemetry":{
            "hp":920,
            "max_hp":1000,
            "chassis_power_w":86.5,
            "buffer_energy_j":44.0,
            "barrel_heat":37,
            "ammo":120,
            "latency_ms":13,
            "packet_loss_pct":0.5
        }
    })json";

    const auto sample = rm27::sim::parseRefereeDatagram(valid);
    assert(sample);
    assert(sample->sequence == 7);
    assert(sample->phase == rm27::MatchPhase::Match);
    assert(sample->hp == 920);
    assert(sample->maxHp == 1000);
    assert(!sample->hud);
    assert(!sample->event);

    QByteArray withEvent = valid;
    withEvent.replace("\"telemetry\":{", R"json("event":{
        "id":"sim-upgrade-30","category":"robot","text":"模拟：英雄机器人升至 3 级"
    },"telemetry":{)json");
    const auto eventSample = rm27::sim::parseRefereeDatagram(withEvent);
    assert(eventSample && eventSample->event);
    assert(eventSample->event->id == QStringLiteral("sim-upgrade-30"));
    assert(eventSample->event->category == QStringLiteral("robot"));
    QByteArray invalidEvent = withEvent;
    invalidEvent.replace("\"category\":\"robot\"", "\"category\":\"official\"");
    assert(!rm27::sim::parseRefereeDatagram(invalidEvent));
    invalidEvent = withEvent;
    invalidEvent.replace("sim-upgrade-30", "bad id");
    assert(!rm27::sim::parseRefereeDatagram(invalidEvent));
    invalidEvent = withEvent;
    invalidEvent.replace("模拟：英雄机器人升至 3 级", "line\\nfeed");
    assert(!rm27::sim::parseRefereeDatagram(invalidEvent));

    QByteArray withHud = valid;
    withHud.replace("\"telemetry\":{", R"json("hud":{
        "own_team":"blue",
        "friendly_hp":[900,800,700,600,500,400],
        "enemy_hp":[880,780,680,580,480,380],
        "friendly_positions":[[0.1,0.2],[0.2,0.3],[0.3,0.4],[0.4,0.5],[0.5,0.6],[0.6,0.7]],
        "enemy_positions":[[0.9,0.8],[0.8,0.7],[0.7,0.6],[0.6,0.5],[0.5,0.4],[0.4,0.3]],
        "friendly_coins":100,"enemy_coins":80,"restart_count":2,
        "pitch_deg":12.5,"yaw_deg":-37.5,"self_index":3,"speed_mps":24.0,"allowed_shots":80
    },"telemetry":{)json");
    const auto hudSample = rm27::sim::parseRefereeDatagram(withHud);
    assert(hudSample && hudSample->hud);
    assert(hudSample->hud->ownTeam == QStringLiteral("blue"));
    assert(hudSample->hud->friendlyHp[0] == 900);
    assert(hudSample->hud->enemyPositions[5][1] == 0.3);
    assert(!hudSample->hud->level && !hudSample->hud->actions);
    // 偏航角为可选字段：带了就取值，数值原样保留。
    assert(hudSample->hud->yawDeg);
    assert(*hudSample->hud->yawDeg < -37.4 && *hudSample->hud->yawDeg > -37.6);
    // 本车下标同样是可选字段：0..5。
    assert(hudSample->hud->selfIndex);
    assert(*hudSample->hud->selfIndex == 3);
    QByteArray withStatus = withHud;
    withStatus.replace("\"allowed_shots\":80", R"json("allowed_shots":80,
        "level":3,"shield":60,"max_shield":100,
        "actions":{"climb":false,"fire":true,"speed_up":false})json");
    const auto statusSample = rm27::sim::parseRefereeDatagram(withStatus);
    assert(statusSample && statusSample->hud);
    assert(statusSample->hud->level == 3);
    assert(statusSample->hud->shield == 60);
    assert(statusSample->hud->actions && statusSample->hud->actions->fire);
    QByteArray invalidStatus = withStatus;
    invalidStatus.replace("\"shield\":60", "\"shield\":120");
    assert(!rm27::sim::parseRefereeDatagram(invalidStatus));
    invalidStatus = withStatus;
    invalidStatus.replace("\"fire\":true", "\"fire\":1");
    assert(!rm27::sim::parseRefereeDatagram(invalidStatus));
    invalidStatus = withStatus;
    invalidStatus.replace("\"level\":3", "\"level\":11");
    assert(!rm27::sim::parseRefereeDatagram(invalidStatus));
    QByteArray invalidHud = withHud;
    invalidHud.replace("\"pitch_deg\":12.5", "\"pitch_deg\":90");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    // 偏航角越界或类型错误：按既有可选字段约定拒绝整包。
    invalidHud = withHud;
    invalidHud.replace("\"yaw_deg\":-37.5", "\"yaw_deg\":181");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    invalidHud = withHud;
    invalidHud.replace("\"yaw_deg\":-37.5", "\"yaw_deg\":\"-37.5\"");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    // 旧模拟包不带 yaw_deg / self_index 时照常解析，两项保持未知（界面显示 -- 与待确认）。
    QByteArray legacyHud = withHud;
    legacyHud.replace(",\"yaw_deg\":-37.5", "");
    legacyHud.replace(",\"self_index\":3", "");
    const auto legacySample = rm27::sim::parseRefereeDatagram(legacyHud);
    assert(legacySample && legacySample->hud && !legacySample->hud->yawDeg
           && !legacySample->hud->selfIndex);
    // 本车下标越界（队列表只有六个位置）或类型错误：拒绝整包。
    invalidHud = withHud;
    invalidHud.replace("\"self_index\":3", "\"self_index\":6");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    invalidHud = withHud;
    invalidHud.replace("\"self_index\":3", "\"self_index\":\"3\"");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    invalidHud = withHud;
    invalidHud.replace("[0.1,0.2]", "[1.1,0.2]");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));
    invalidHud = withHud;
    invalidHud.replace("\"own_team\":\"blue\"", "\"own_team\":\"green\"");
    assert(!rm27::sim::parseRefereeDatagram(invalidHud));

    assert(!rm27::sim::parseRefereeDatagram("{}"));
    assert(!rm27::sim::parseRefereeDatagram("{not-json}"));

    QByteArray wrongChannel = valid;
    wrongChannel.replace("rm27.sim.v1.referee", "official-looking-name");
    assert(!rm27::sim::parseRefereeDatagram(wrongChannel));

    QByteArray invalidHp = valid;
    invalidHp.replace("\"hp\":920", "\"hp\":1200");
    assert(!rm27::sim::parseRefereeDatagram(invalidHp));

    std::cout << "rm27 simulation protocol checks passed\n";
    return 0;
}
