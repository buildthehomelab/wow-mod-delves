#include "DelveCart.h"
#include <iostream>
#include <limits>

using namespace Evermore::Delves;

void Check(bool value, char const* why) { if (!value) throw std::runtime_error(why); }
Cart Start() { return Cart(7, 42, 9, {{0,0,10}, {10,0,10}, {10,10,10}}, 10, 1, 2, 2000); }
int main()
{
    auto victim = CartVictim{7,42,9,{5,0,10}};
    auto cart = Start();
    Check(cart.Advance(1999, &victim).stage == CartStage::Warning, "warning not preserved");
    Check(!cart.Advance(1, &victim).impact, "cart damaged distant player before movement");
    Check(cart.Advance(1000, &victim).impact, "fast cart skipped player");
    Check(!cart.Advance(1000, &victim).impact, "one cart paid damage twice");
    Check(!cart.Advance(std::numeric_limits<std::uint32_t>::max(), &victim).impact, "finished cart damaged again");
    cart = Start();
    victim.point = {10,5,10};
    auto end = cart.Advance(std::numeric_limits<std::uint32_t>::max(), &victim);
    Check(end.stage == CartStage::Finished && end.impact && end.point.y == 10, "lag spike missed curved path or overflowed");
    Check(end.impactOrigin.x == 10 && std::abs(end.impactOrigin.y - 4) < 0.000001,
        "lagged impact origin followed final position rather than first contact");
    cart = Start();victim.point={5,0,13};
    Check(!cart.Advance(4000,&victim).impact,"cart hit another floor");
    cart = Start();victim.point={5,2,10};
    Check(!cart.Advance(4000,&victim).impact,"sidestep failed");
    cart = Start();victim.point={5,0,10};victim.account=8;
    Check(!cart.Advance(4000,&victim).impact,"cart hit different account");
    cart = Start();victim.account=9;victim.run=8;
    Check(!cart.Advance(4000,&victim).impact,"cart crossed run identity");
    Cart slope(7,42,9,{{0,0,0},{10,0,10}},10,1,0.1,0);
    victim={7,42,9,{5,0,5.5}};
    Check(slope.Advance(2000,&victim).impact,"slope intersection missed away from XY closest point");
    Cart vertical(7,42,9,{{0,0,0},{0,0,10}},10,1,0.1,0);
    victim.point={0,0,5};
    Check(vertical.Advance(2000,&victim).impact,"vertical sweep missed height interval");
    Cart occluded(7,42,9,{{0,0,0},{10,0,0}},1,1,1,0);
    victim.point={1,0,0};
    unsigned checks=0;
    Check(!occluded.Advance(500,&victim,[&](CartPoint const& origin, CartPoint const& target)
        { ++checks; Check(origin.x == 0 && target.x == 1,"visibility checked wrong contact"); return false; }).impact,
        "occluded cart contact dealt damage");
    Check(checks == 1,"contact visibility was not checked");
    Check(occluded.Advance(500,&victim,[](CartPoint const&, CartPoint const&) { return true; }).impact,
        "occluded contact consumed later exposed impact");
    bool rejected=false;
    try { Cart bad(7,42,9,{{0,0,0},{std::numeric_limits<double>::infinity(),0,0}},10,1,2,2000); }
    catch(std::invalid_argument const&) { rejected=true; }
    Check(rejected,"nonfinite lane accepted");
    std::cout << "cart warning, swept path, lag, floor and identity checks passed\n";
}
