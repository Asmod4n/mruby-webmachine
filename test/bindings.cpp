#include <mruby.h>

#include "config.hpp"
#include "http.hpp"
#include "problem.hpp"

extern "C" void mrb_mruby_webmachine_gem_test(mrb_state *mrb)
{
    config_spec(mrb);
    http_spec(mrb);
    problem_spec(mrb);
}
