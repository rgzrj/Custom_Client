#include "protocol/rm2026/upstream_backend.hpp"
#include "protocol/rm2026/state_mapping.hpp"
#include "network/NetworkManager.h"
#include <QThread>
#include <QTimer>
#include <QJsonObject>
#include <mutex>

namespace rm27::protocol::rm2026 {
using historical::rm2026::Command;
using historical::rm2026::CommandContext;
using historical::rm2026::KeyboardMouseCommand;
using Clock = std::chrono::steady_clock;
struct UpstreamBackend::Impl {
    QThread thread;
    QObject *worker = new QObject;
    GameData *data = nullptr;
    RM::NetworkManager *network = nullptr;
    QTimer *timer = nullptr;
    QTimer *inputTimer = nullptr;
    mutable std::mutex mutex;
    BackendView latest;
    quint64 generation = 0;
    QString error;
    struct Pending {
        Command command;
        CommandContext context;
        Clock::time_point at;
        quint64 serial;
    };
    std::optional<Pending> input, release, event;
    KeyboardMouseCommand desired;
    CommandContext inputContext;
    bool inputOpen = false, cancelPending = false;
    Clock::time_point lastInputAt;
    quint64 serial = 0, replaced = 0;
    quint64 admitted = 0, acknowledged = 0, unknown = 0;
    QString commandStatus;
    QString eventStatus;
    int eventToken = 0; // Only the most recently admitted event; input ACKs cannot update it.
    quint64 eventsRejected = 0;

    // Called under mutex. No stale input is retained across source/session/gate changes.
    void clearInput() {
        if (event) eventStatus = "event_cancelled";
        input.reset(); release.reset(); event.reset(); desired = {};
        inputOpen = false;
        inputContext = {};
    }
    void sendPending(quint64 session, bool eventOnly = false) {
        std::optional<Pending> next;
        int slot = 0;
        bool cancel = false;
        {
            std::lock_guard lock(mutex);
            if (session != generation) return;
            const bool expired = inputOpen
                && Clock::now() - lastInputAt >= std::chrono::milliseconds(100);
            cancel = cancelPending || expired;
            if (expired) commandStatus = "input_expired";
            cancelPending = false;
            if (cancel) clearInput();
            else if (eventOnly) { next = event; slot = 2; }
            else if (release) { next = release; slot = 1; }
            else next = input;
            if (next && Clock::now() - next->at >= std::chrono::milliseconds(100)) {
                if (slot == 2) {
                    event.reset(); next.reset(); ++eventsRejected;
                    eventStatus = "event_expired";
                } else {
                    clearInput(); next.reset(); cancel = true;
                    commandStatus = "input_expired";
                }
            }
        }
        if (cancel) { network->cancelCommands(); return; }
        if (!next) return;
        const auto result = network->sendCommand(next->command, next->context);
        const bool localEventRejection = slot == 2
            && (result.status == CommandSendStatus::Blocked || result.status == CommandSendStatus::Invalid);
        bool preserveInput = false;
        if (localEventRejection) {
            std::optional<CommandContext> activeContext;
            {
                std::lock_guard lock(mutex);
                if (session != generation) return;
                if (inputOpen) activeContext = inputContext;
            }
            // NetworkManager's session callback also takes this mutex.
            preserveInput = !activeContext || network->keyboardInputAllowed(*activeContext);
        }
        bool invalidate = false;
        {
            std::lock_guard lock(mutex);
            if (session != generation) return;
            commandStatus = result.reason;
            if (result.status == CommandSendStatus::Queued) ++admitted;
            if (result.status == CommandSendStatus::RateLimited
                || result.status == CommandSendStatus::InFlightLimit) return;
            auto &selected = slot == 1 ? release : slot == 2 ? event : input;
            if (selected && selected->serial == next->serial) selected.reset();
            if (slot == 2) {
                eventStatus = result.reason;
                eventToken = result.status == CommandSendStatus::Queued ? result.token : 0;
            }
            if (result.status != CommandSendStatus::Queued) {
                if (slot == 2) ++eventsRejected;
                // Business rejection of an event does not revoke valid ongoing input.
                // A shared gate change or transport failure still cancels the session.
                invalidate = !preserveInput;
                if (invalidate) clearInput();
            }
        }
        if (invalidate) network->cancelCommands();
    }

    Impl() {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    ~Impl() {
        { std::lock_guard lock(mutex); ++generation; clearInput(); }
        QMetaObject::invokeMethod(worker, [this] {
            delete network;
            delete data;
            thread.quit();
        }, Qt::QueuedConnection);
        thread.wait();
    }
};

UpstreamBackend::UpstreamBackend() : impl_(std::make_unique<Impl>()) {}
UpstreamBackend::~UpstreamBackend() = default;

void UpstreamBackend::start(const QString &host, quint16 port, unsigned robotId) {
    auto *p = impl_.get();
    quint64 generation;
    {
        std::lock_guard lock(p->mutex);
        generation = ++p->generation;
        p->clearInput(); p->cancelPending = false;
        p->replaced = 0;
        p->latest = {};
        p->latest.status = {{"link", "connecting"}, {"protocol", "rm2026-v2.0.0"},
                            {"client_id", QString::number(robotId)}};
    }
    QMetaObject::invokeMethod(p->worker, [p, host, port, robotId, generation] {
        { std::lock_guard lock(p->mutex); if (generation != p->generation) return; }
        delete p->timer; p->timer = nullptr;
        delete p->inputTimer; p->inputTimer = nullptr;
        delete p->network; p->network = nullptr;
        p->error.clear();
        p->admitted = p->acknowledged = p->unknown = 0;
        p->commandStatus.clear();
        p->eventStatus.clear(); p->eventToken = 0; p->eventsRejected = 0;
        // Keep the single state owner across endpoint/identity/source changes.
        // ponytail: backend-wide constraint; per-robot release needs official evidence.
        if (!p->data) p->data = new GameData(p->worker);
        p->data->resetOfficialReports();
        p->data->setMyRobotId(int(robotId)); // Requested perspective, not a reported identity.
        p->network = new RM::NetworkManager(p->data, p->worker);
        p->network->setSubscriptionCurrent([p, generation] {
            std::lock_guard lock(p->mutex);
            return generation == p->generation;
        });
        QObject::connect(p->network, &RM::NetworkManager::errorOccurred, p->worker,
                         [p](const QString &error) { p->error = error; });
        QObject::connect(p->network, &RM::NetworkManager::subscriptionStateChanged, p->worker,
                         [p](bool subscribed) {
            if (subscribed) p->error.clear();
            else { std::lock_guard lock(p->mutex); p->clearInput(); }
        });
        QObject::connect(p->network, &RM::NetworkManager::commandDelivery, p->worker,
                         [p, generation](int token, bool brokerAcknowledged) {
            std::lock_guard lock(p->mutex);
            if (generation != p->generation) return;
            if (brokerAcknowledged) ++p->acknowledged; else ++p->unknown;
            p->commandStatus = brokerAcknowledged ? "broker_acknowledged" : "delivery_unknown";
            if (token == p->eventToken) {
                p->eventStatus = p->commandStatus;
                p->eventToken = 0;
            }
        });
        p->inputTimer = new QTimer(p->worker);
        p->inputTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(p->inputTimer, &QTimer::timeout, p->worker,
                         [p, generation] {
            p->sendPending(generation);
            p->sendPending(generation, true); // An event cannot stall continuous input.
        });
        p->inputTimer->start(14);
        p->timer = new QTimer(p->worker);
        QObject::connect(p->timer, &QTimer::timeout, p->worker, [p, generation, robotId] {
            BackendView view;
            view.subscribed = p->network->topicsSubscribed() && p->network->isMqttConnected();
            if (view.subscribed) view.snapshot = p->data->officialSnapshot();
            else view.snapshot.penaltyReleaseUnverified = p->data->officialSnapshot().penaltyReleaseUnverified;
            view.status = {{"protocol", "rm2026-v2.0.0"}, {"client_id", QString::number(robotId)},
                {"link", view.subscribed ? "subscribed" : "offline"}, {"error", p->error},
                {"subscription_count", view.subscribed ? 24 : 0}, {"blocked_topics", QStringList{"RadarInfoToClient"}},
                {"accepted", qulonglong(p->data->officialAccepted())}, {"rejected", qulonglong(p->data->officialRejected())},
                {"reports", view.subscribed ? p->data->officialReports() : QVariantMap{}},
                {"state", snapshotJson(view.snapshot).toVariantMap()}};
            auto missing = GameData::officialTopics();
            const auto reports = view.status.value("reports").toMap();
            for (auto it = reports.cbegin(); it != reports.cend(); ++it) missing.removeAll(it.key());
            view.status.insert("missing_or_stale_topics", missing);
            view.status.insert("mqtt_connected", p->network->isMqttConnected());
            std::lock_guard lock(p->mutex);
            if (generation == p->generation) {
                view.status.insert("commands", QVariantMap{
                    {"status", p->commandStatus}, {"admitted", qulonglong(p->admitted)},
                    {"broker_acknowledged", qulonglong(p->acknowledged)},
                    {"delivery_unknown", qulonglong(p->unknown)},
                    {"in_flight", p->network->commandsInFlight()},
                    {"latest_input_depth", p->input ? 1 : 0},
                    {"release_depth", p->release ? 1 : 0},
                    {"event_depth", p->event ? 1 : 0}, {"replaced", qulonglong(p->replaced)},
                    {"event_status", p->eventStatus}, {"events_rejected", qulonglong(p->eventsRejected)}});
                p->latest = std::move(view);
            }
        });
        p->timer->start(50);
        p->network->startMqtt(QStringLiteral("tcp://%1:%2").arg(host).arg(port), QString::number(robotId));
    }, Qt::QueuedConnection);
}

BackendView UpstreamBackend::view() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->latest;
}
void UpstreamBackend::stop() {
    auto *p = impl_.get();
    { std::lock_guard lock(p->mutex); ++p->generation; p->latest = {}; p->clearInput(); }
    QMetaObject::invokeMethod(p->worker, [p] {
        delete p->timer; p->timer = nullptr;
        delete p->inputTimer; p->inputTimer = nullptr;
        delete p->network; p->network = nullptr;
        if (p->data) p->data->resetOfficialReports();
    }, Qt::QueuedConnection);
}

void UpstreamBackend::updateInput(std::optional<KeyboardMouseCommand> input,
                                  const CommandContext &context) {
    auto *p = impl_.get();
    const bool allowed = input && commandBlock(*input, context).isEmpty();
    std::lock_guard lock(p->mutex);
    if (!allowed || !p->latest.subscribed) {
        p->cancelPending = true;
        p->clearInput();
        return;
    }
    const auto now = Clock::now();
    const auto old = p->desired;
    if ((old.keys & ~input->keys) || (old.left && !input->left)
        || (old.right && !input->right) || (old.middle && !input->middle)) {
        KeyboardMouseCommand neutral;
        neutral.keys = old.keys & input->keys;
        neutral.left = old.left && input->left;
        neutral.right = old.right && input->right;
        neutral.middle = old.middle && input->middle;
        if (p->release) {
            const auto previous = std::get<KeyboardMouseCommand>(p->release->command);
            neutral.keys &= previous.keys; neutral.left &= previous.left;
            neutral.right &= previous.right; neutral.middle &= previous.middle;
        }
        p->release = Impl::Pending{neutral, context, now, ++p->serial};
    }
    if (p->input) ++p->replaced;
    p->input = Impl::Pending{*input, context, now, ++p->serial};
    p->desired = *input; p->inputOpen = true; p->lastInputAt = now;
    p->inputContext = context;
}

CommandSendResult UpstreamBackend::submitCommand(const Command &command,
                                                const CommandContext &context) {
    if (std::holds_alternative<KeyboardMouseCommand>(command))
        return {CommandSendStatus::Invalid, "use_latest_input"};
    const auto block = commandBlock(command, context);
    if (!block.isEmpty()) return {CommandSendStatus::Blocked, block};
    const auto encoded = encodeCommand(command, context.mapOperator);
    if (!encoded.error.isEmpty()) return {CommandSendStatus::Invalid, encoded.error};
    std::lock_guard lock(impl_->mutex);
    if (!impl_->latest.subscribed) return {CommandSendStatus::Disconnected, "mqtt_unsubscribed"};
    // Reject known ineligible actions before occupying the bounded event slot.
    // NetworkManager still re-reads actual reports immediately before publishing.
    const auto businessBlock = historical::rm2026::businessCommandBlock(
        command, impl_->latest.snapshot, context, Clock::now());
    if (*businessBlock) return {CommandSendStatus::Blocked, QString::fromLatin1(businessBlock)};
    if (impl_->event) return {CommandSendStatus::InFlightLimit, "event_slot_busy"};
    impl_->event = Impl::Pending{command, context, Clock::now(), ++impl_->serial};
    impl_->eventStatus = "awaiting_worker";
    impl_->eventToken = 0;
    return {CommandSendStatus::Queued, "awaiting_worker"};
}
}
