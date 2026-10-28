#ifndef EVERMORE_DELVE_CART_H
#define EVERMORE_DELVE_CART_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Evermore::Delves
{
    struct CartPoint { double x, y, z; };
    enum class CartStage { Warning, Moving, Finished };
    struct CartVictim
    {
        std::uint64_t run;
        std::uint32_t guid;
        std::uint32_t account;
        CartPoint point;
    };
    struct CartStep { CartPoint point; CartStage stage; bool impact; CartPoint impactOrigin; };

    // Single admitted-owner hazard. The adapter must additionally validate live
    // player/instance/phase/run eligibility before supplying a victim each tick.
    class Cart
    {
    public:
        Cart(std::uint64_t run, std::uint32_t guid, std::uint32_t account,
            std::vector<CartPoint> lane, double speed, double radius, double verticalTolerance,
            std::uint32_t warningMs)
            : _run(run), _guid(guid), _account(account), _lane(std::move(lane)), _speed(speed),
              _radius(radius), _height(verticalTolerance), _warning(warningMs)
        {
            if (!run || !guid || !account || _lane.size() < 2 || _lane.size() > 16 ||
                !std::isfinite(speed) || speed < 1 || speed > 30 || !std::isfinite(radius) ||
                radius <= 0 || radius > 10 || !std::isfinite(verticalTolerance) ||
                verticalTolerance <= 0 || verticalTolerance > 5 || warningMs > 10000)
                throw std::invalid_argument("invalid cart hazard");
            for (auto const& point : _lane)
                if (!Finite(point))
                    throw std::invalid_argument("non-finite cart lane");
            _lengths.push_back(0);
            for (std::size_t index = 1; index < _lane.size(); ++index)
            {
                double length = Distance(_lane[index - 1], _lane[index]);
                if (!std::isfinite(length) || length < 0.1)
                    throw std::invalid_argument("degenerate cart segment");
                _lengths.push_back(_lengths.back() + length);
            }
            if (_lengths.back() > 200)
                throw std::invalid_argument("cart lane too long");
            _finish = _warning + static_cast<std::uint32_t>(std::ceil(_lengths.back() / _speed * 1000));
        }

        using ContactAllowed = std::function<bool(CartPoint const&, CartPoint const&)>;
        CartStep Advance(std::uint32_t diff, CartVictim const* victim = nullptr,
            ContactAllowed const& allowed = {})
        {
            auto previous = _elapsed;
            _elapsed += std::min(diff, _finish - _elapsed);
            auto distance = Travel(_elapsed);
            bool impact = false;
            CartPoint origin{};
            // A warning boundary or zero-duration update contains no movement.
            // Only sweep once the cart has actually advanced into its lane.
            if (!_hit && _elapsed > previous && _elapsed > _warning && victim &&
                victim->run == _run && victim->guid == _guid && victim->account == _account && Finite(victim->point))
            {
                double from = Travel(previous);
                for (std::size_t index = 1; index < _lane.size(); ++index)
                {
                    double begin = std::max(from, _lengths[index - 1]);
                    double end = std::min(distance, _lengths[index]);
                    if (begin <= end && Touches(At(begin), At(end), victim->point, origin) &&
                        (!allowed || allowed(origin, victim->point)))
                    {
                        impact = _hit = true;
                        break;
                    }
                }
            }
            return {At(distance), _elapsed < _warning ? CartStage::Warning :
                _elapsed < _finish ? CartStage::Moving : CartStage::Finished, impact, origin};
        }

    private:
        static bool Finite(CartPoint const& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
        static double Distance(CartPoint const& a, CartPoint const& b)
        {
            return std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z);
        }
        double Travel(std::uint32_t elapsed) const
        {
            return elapsed < _warning ? 0 : std::min(_lengths.back(), (elapsed - _warning) * _speed / 1000);
        }
        CartPoint At(double distance) const
        {
            for (std::size_t index = 1; index < _lane.size(); ++index)
                if (distance <= _lengths[index])
                {
                    double t = (distance - _lengths[index - 1]) / (_lengths[index] - _lengths[index - 1]);
                    auto a = _lane[index - 1];
                    auto b = _lane[index];
                    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                }
            return _lane.back();
        }
        bool Touches(CartPoint a, CartPoint b, CartPoint p, CartPoint& origin) const
        {
            double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            double squared = dx * dx + dy * dy;
            double low = 0, high = 1;
            if (squared)
            {
                double ox = p.x - a.x, oy = p.y - a.y;
                double center = (ox * dx + oy * dy) / squared;
                double perpendicular = std::abs(ox * dy - oy * dx) / std::sqrt(squared);
                if (!std::isfinite(center) || !std::isfinite(perpendicular) || perpendicular > _radius)
                    return false;
                double half = std::sqrt(std::max(0.0, _radius * _radius - perpendicular * perpendicular) / squared);
                low = std::max(low, center - half);
                high = std::min(high, center + half);
            }
            else if (std::hypot(p.x - a.x, p.y - a.y) > _radius)
                return false;
            if (dz)
            {
                double first = (p.z - _height - a.z) / dz;
                double last = (p.z + _height - a.z) / dz;
                low = std::max(low, std::min(first, last));
                high = std::min(high, std::max(first, last));
            }
            else if (std::abs(p.z - a.z) > _height)
                return false;
            if (low > high)
                return false;
            origin = {a.x + dx * low, a.y + dy * low, a.z + dz * low};
            return true;

        }
        std::uint64_t _run;
        std::uint32_t _guid, _account;
        std::vector<CartPoint> _lane;
        std::vector<double> _lengths;
        double _speed, _radius, _height;
        std::uint32_t _warning, _finish = 0, _elapsed = 0;
        bool _hit = false;
    };
}
#endif
