#include <stdio.h>

#include "test_config.h"

int g_test_failures;

static void run_group(const char *name, void (*fn)(void))
{
  printf("[ RUN      ] %s\n", name);
  fn();
  printf("[     DONE ] %s\n", name);
}

int main(void)
{
  run_group("adc_protocol", test_adc_protocol_run);
  run_group("frame_protocol", test_frame_protocol_run);
  run_group("usb_stream", test_usb_stream_run);
  run_group("app_smoke", test_app_smoke_run);

  if (g_test_failures != 0)
  {
    printf("[  FAILED  ] %d assertion(s)\n", g_test_failures);
    return 1;
  }

  printf("[  PASSED  ] host tests\n");
  return 0;
}
