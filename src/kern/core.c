/*
    Orion OS, The educational operatingsystem
    Copyright (C) 2011  Bart Kuivenhoven

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <kern/core.h>
#include <kern/sched.h>
#include <fs/stream.h>

#define RL_BOOT     0x0
#define RL_SHUTDOWN 0x1
#define RL_RUN0     0x2
#define RL_RUN1     0x3
#define RL_RUN2     0x4
#define RL_RUN3     0x5
#define RL_REBOOT   0x6

void shutdown()
{
  printf("You can now shutdown your PC\n");
  for(;;)
  {
    endProg();
  }
}

volatile uint32_t rl = 0;

void init_set(uint32_t i)
{
  rl = i;
}

void stream_test()
{
  stream *test = stream_open();
  stream_write(test, "Hello streams!\n");
  stream_seek(test, 0, SEEK_SET);
  printf("%s", stream_read(test, strlen("Hello streams!\n")));
  stream_close(test);
}

void core_loop()
{
  uint32_t pid = 0;

  while (TRUE) // Infinite loop, to make the kernel wait when there is nothing to do
  {
    switch (rl)
    {
      case RL_BOOT:
//         pid = fork();
        init_set(RL_RUN0);
        break;

      case RL_RUN0:
#ifdef STREAM_DBG
        stream_test();
        init_set(RL_RUN1);
#endif
      case RL_RUN1:
      case RL_RUN2:
      case RL_RUN3:
//         halt();
//         sched_next_task();
        break;

      case RL_REBOOT:
        reboot();
        break;

      case RL_SHUTDOWN:
        shutdown();
        break;
    }
    halt(); // Puts the CPU in idle state untill next interrupt
  }
}