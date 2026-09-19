#include <mruby.h>

#include "http.hpp"

extern "C" void mrb_mruby_webmachine_gem_test(mrb_state *mrb)
{
    http_spec(mrb);
}
