#include "tile.h"
#include "mem_component.h"
#include "tile_manager.h"
#include "simulator.h"

#include "carbon_user.h"
#include "fixed_types.h"

using namespace std;

void* thread_func(void*);

IntPtr address = 0x1000;
int num_threads = 100;
int num_iterations = 100;

carbon_barrier_t barrier;

int main (int argc, char *argv[])
{
   printf("Starting (shared_mem_test2)\n");
   // Start simulator
   CarbonStartSim(argc, argv);
   // Enable performance models
   Simulator::enablePerformanceModelsInCurrentProcess();

   // Init barrier
   CarbonBarrierInit(&barrier, num_threads);

   carbon_thread_t tid_list[num_threads];

   Core* core = Sim()->getTileManager()->getCurrentTile()->getCore();
   int val = 0;
   core->initiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::WRITE, address, (Byte*) &val, sizeof(val), true);

   for (int i = 0; i < num_threads-1; i++)
   {
      tid_list[i] = CarbonSpawnThread(thread_func, NULL);
   }
   thread_func(NULL);

   for (int i = 0; i < num_threads-1; i++)
   {
      CarbonJoinThread(tid_list[i]);
   }
   
   core->initiateMemoryAccess(MemComponent::L1_DCACHE, Core::NONE, Core::READ, address, (Byte*) &val, sizeof(val), true);
   
   printf("val(%i)\n", val);
   if (val == (num_threads * num_iterations))
   {
      printf("shared_mem_test2 (SUCCESS)\n");
   }
   else
   {
      printf("shared_mem_test2 (FAILURE)\n");
   }
  
   // Disable performance models
   Simulator::disablePerformanceModelsInCurrentProcess();
   // Shutdown simulator 
   CarbonStopSim();

   return 0;
}

void* thread_func(void*)
{
   // Wait on barrier
   CarbonBarrierWait(&barrier);

   Core* core = Sim()->getTileManager()->getCurrentTile()->getCore();

   for (int i = 0; i < num_iterations; i++)
   {
      int val;
      core->initiateMemoryAccess(MemComponent::L1_DCACHE, Core::LOCK, Core::READ_EX, address, (Byte*) &val, sizeof(val), true);
      
      val += 1;

      core->initiateMemoryAccess(MemComponent::L1_DCACHE, Core::UNLOCK, Core::WRITE, address, (Byte*) &val, sizeof(val), true);
   }
}
