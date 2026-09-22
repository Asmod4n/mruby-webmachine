#include <mruby.h>

#include "webmachine.hpp"

extern "C" {

void mrb_mruby_webmachine_gem_init(mrb_state *mrb)
{
    (void) mrb;
}

void mrb_mruby_webmachine_gem_final(mrb_state *mrb)
{
    (void) mrb;
}
}
