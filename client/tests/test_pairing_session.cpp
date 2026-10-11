#include "core/pairing_session.hpp"
#include <cassert>
#include <initializer_list>
#include <limits>

using namespace rm27;

int main()
{
    PairingSession session;
    assert(session.phase(0) == PairingPhase::Idle);
    for (auto region : {PairingRegion::Global, PairingRegion::Japan}) {
        assert(session.begin(region, 103, 0, 1000));
        const auto token = session.token();
        assert(session.phase(0) == PairingPhase::ExternalRequired);
        assert(session.observeVideo(token, 10));
        assert(!session.operatorConfirmed()); // Valid pixels never assert hardware pairing.
        assert(session.phase(10) == PairingPhase::ExternalRequired);
        assert(session.confirmExternal(token, 20));
        assert(session.phase(20) == PairingPhase::VideoReview);
        assert(!session.reportedId()); // Neither operator report nor pixels authenticates an ID.
        session.tick(2000);
        assert(session.phase(2000) == PairingPhase::VideoRequired);
        assert(session.operatorConfirmed()); // Silence does not prove loss of hardware pairing.
        assert(session.confirmExternal(token, 2000));
        assert(session.observeVideo(token, 2010));
        assert(session.phase(2010) == PairingPhase::VideoReview);
        assert(session.videoFresh(2709));
        assert(!session.videoFresh(2710));
        assert(session.videoUnavailable(token, 2711));
        assert(session.phase(2711) == PairingPhase::VideoRequired);
        assert(session.deviceDisconnected(token));
        assert(session.phase(2711) == PairingPhase::Disconnected);
        assert(!session.operatorConfirmed() && !session.videoFresh(2711));
        assert(!session.observeVideo(token, 2720));
    }
    assert(session.begin(PairingRegion::Global, 1, 3000, 100));
    const auto old = session.token();
    session.tick(3100);
    assert(session.phase(3100) == PairingPhase::TimedOut);
    assert(!session.confirmExternal(old, 3101));
    assert(!session.observeVideo(old, 3101));
    assert(session.begin(PairingRegion::Global, 101, 3200));
    const auto current = session.token();
    assert(!session.reportIdentity(old, 1, 3201));
    assert(session.reportIdentity(current, 1, 3201));
    assert(session.phase(3201) == PairingPhase::IdentityMismatch);
    assert(!session.confirmExternal(current, 3202));
    assert(session.reportIdentity(current, 101, 3203));
    assert(session.phase(3203) == PairingPhase::ExternalRequired);
    assert(session.confirmExternal(current, 3204));
    assert(session.phase(3204) == PairingPhase::VideoRequired);
    assert(session.reportIdentity(current, {}, 3205));
    assert(!session.reportedId());
    session.cancel();
    assert(session.phase(3206) == PairingPhase::Cancelled);
    assert(!session.confirmExternal(current, 3206));
    const auto cancelled = session.token();
    for (auto id : {0u, 5u, 8u, 100u, 105u, 108u, 4294967295u}) {
        assert(!session.begin(PairingRegion::Global, id, 3300));
        assert(session.token() == cancelled); // Bad input does not replace the session.
    }
    assert(!session.begin(PairingRegion::Global, 1, 0, 0));
    assert(!session.begin(PairingRegion::Global, 1, -1));
    assert(!session.begin(static_cast<PairingRegion>(99), 1, 0));
    assert(!session.begin(PairingRegion::Global, 1,
        std::numeric_limits<std::int64_t>::max()));
    assert(session.begin(PairingRegion::Japan, 3, 4000, 100));
    const auto mismatched = session.token();
    assert(session.reportIdentity(mismatched, 103, 4001));
    session.tick(4100);
    assert(session.phase(4100) == PairingPhase::TimedOut);
    assert(!session.reportIdentity(mismatched, 3, 4101));
}
