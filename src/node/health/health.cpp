#include "health.hpp"
void Health::run()
{
    std::unique_lock l(m);
    while (!finished) {
        cv.wait(l, [&]() { return !queue.empty(); });
        auto events { std::move(queue) };
        queue.clear();
        l.unlock();
        for (auto& e : events) {
            std::move(e).visit([&](auto&& event) { handle_event(std::move(event)); });
        }
    }
}
void Health::handle_event(SetEv&& e)
{
    if (e.overwrite) {
        map.insert_or_assign(e.key, e.message);
    } else {
        map.insert(e.key, e.message);
    }
}
void Health::handle_event(RemoveEv&& e)
{
    if (auto it { map.find(e.key) }; it != map.end())
        map.erase(it);
}
void Health::handle_event(HealthCb&& e) const
{
    api::HealthState out;
    for (auto& p : map) {
        out.messages.push_back(p.second);
    }
    e(std::move(out));
}
