#include "thread_start.h"
#include "log.h"
#include "core.h"
#include "simulator.h"
#include "fixed_types.h"
#include "pin_config.h"
#include "core_manager.h"
#include "thread_manager.h"
#include "thread_support.h"

#include <sys/mman.h>

int spawnThreadSpawner(CONTEXT *ctxt)
{
   int res;

   ADDRINT reg_eip = PIN_GetContextReg(ctxt, REG_INST_PTR);

   PIN_LockClient();
   
   AFUNPTR thread_spawner;
   IMG img = IMG_FindByAddress(reg_eip);
   RTN rtn = RTN_FindByName(img, "CarbonSpawnThreadSpawner");
   thread_spawner = RTN_Funptr(rtn);

   PIN_UnlockClient();

   PIN_CallApplicationFunction(ctxt,
            PIN_ThreadId(),
            CALLINGSTD_DEFAULT,
            thread_spawner,
            PIN_PARG(int), &res,
            PIN_PARG_END());

   LOG_PRINT ("Thread spawner spawned");
   LOG_ASSERT_ERROR(res == 0, "Failed to spawn Thread Spawner");

   return res;
}

VOID copyStaticData(IMG& img)
{
   Core* core = Sim()->getCoreManager()->getCurrentCore();
   LOG_ASSERT_ERROR (core != NULL, "Does not have a valid Core ID");

   for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
   {
      ADDRINT sec_address;

      // I am not sure whether we want ot copy over all the sections or just the
      // sections which are relevant like the sections below: DATA, BSS, GOT
      
      // Copy all the mapped sections except the executable section now
      SEC_TYPE sec_type = SEC_Type(sec);
      if (sec_type != SEC_TYPE_EXEC)
      {
         if (SEC_Mapped(sec))
         {
            sec_address = SEC_Address(sec);

            LOG_PRINT ("\nCopying Section: %s at Address: 0x%x of Size: %u to Simulated Memory\n", SEC_Name(sec).c_str(), (UInt32) sec_address, (UInt32) SEC_Size(sec));
            core->accessMemory(Core::NONE, WRITE, sec_address, (char*) sec_address, SEC_Size(sec));
         }
      }
   }
}

VOID copyInitialStackData(ADDRINT& reg_esp, core_id_t core_id)
{
   // We should not get core_id for this stack_ptr
   Core* core = Sim()->getCoreManager()->getCurrentCore();
   LOG_ASSERT_ERROR (core != NULL, "Does not have a valid Core ID");

   // 1) Command Line Arguments
   // 2) Environment Variables
   // 3) Auxiliary Vector Entries

   SInt32 initial_stack_size = 0;

   ADDRINT stack_ptr_base;
   ADDRINT stack_ptr_top;
   
   ADDRINT params = reg_esp;

   int argc = * ((int *) params);
   char **argv = (char **) (params + sizeof(int));
   char **envir = argv+argc+1;

   //////////////////////////////////////////////////////////////////////
   // Pass 1
   // Determine the Initial Stack Size
   // Variables: size_argv_ptrs, size_env_ptrs, size_information_block 
   //////////////////////////////////////////////////////////////////////

   // Write argc
   initial_stack_size += sizeof(argc);

   // Write argv
   for (SInt32 i = 0; i < argc; i++)
   {
      // Writing argv[i]
      initial_stack_size += sizeof(char*);
      initial_stack_size += (strlen(argv[i]) + 1);
   }

   // A '0' at the end 
   initial_stack_size += sizeof(char*);

   // We need to copy over the environmental parameters also
   for (SInt32 i = 0; ; i++)
   {
      // Writing environ[i]
      initial_stack_size += sizeof(char*);
      if (envir[i] == 0)
      {
         break;
      }
      initial_stack_size += (strlen(envir[i]) + 1);
   }

   // Auxiliary Vector Entry
   initial_stack_size += sizeof(Elf32_auxv_t);

   //////////////////////////////////////////////////////////////////////
   // Pass 2
   // Copy over the actual data
   // Variables: stack_ptr_base_sim
   //////////////////////////////////////////////////////////////////////
   

   PinConfig::StackAttributes stack_attr;
   PinConfig::getSingleton()->getStackAttributesFromCoreID (core_id, stack_attr);
   stack_ptr_top = stack_attr.lower_limit + stack_attr.size;
   stack_ptr_base = stack_ptr_top - initial_stack_size;
   stack_ptr_base = (stack_ptr_base >> (sizeof(ADDRINT))) << (sizeof(ADDRINT));
   
   // Assign the new ESP
   reg_esp = stack_ptr_base;

   // fprintf (stderr, "argc = %d\n", argc);
   // Write argc
   core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &argc, sizeof(argc));
   stack_ptr_base += sizeof(argc);

   LOG_PRINT("Copying Command Line Arguments to Simulated Memory\n");
   for (SInt32 i = 0; i < argc; i++)
   {
      // Writing argv[i]
      stack_ptr_top -= (strlen(argv[i]) + 1);
      core->accessMemory(Core::NONE, WRITE, stack_ptr_top, (char*) argv[i], strlen(argv[i])+1);

      core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &stack_ptr_top, sizeof(stack_ptr_top));
      stack_ptr_base += sizeof(stack_ptr_top);
   }

   // I have found this to be '0' in most cases
   core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &argv[argc], sizeof(argv[argc]));
   stack_ptr_base += sizeof(argv[argc]);

   // We need to copy over the environmental parameters also
   LOG_PRINT("Copying Environmental Variables to Simulated Memory\n");
   for (SInt32 i = 0; ; i++)
   {
      // Writing environ[i]
      if (envir[i] == 0)
      {
         core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &envir[i], sizeof(envir[i]));
         stack_ptr_base += sizeof(envir[i]);
         break;
      }

      stack_ptr_top -= (strlen(envir[i]) + 1);
      core->accessMemory(Core::NONE, WRITE, stack_ptr_top, (char*) envir[i], strlen(envir[i])+1);

      core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &stack_ptr_top, sizeof(stack_ptr_top));
      stack_ptr_base += sizeof(stack_ptr_top);
   }
   

   LOG_PRINT("Copying Auxiliary Vector to Simulated Memory\n");
   
   Elf32_auxv_t auxiliary_vector_entry_null;
   auxiliary_vector_entry_null.a_type = AT_NULL;
   auxiliary_vector_entry_null.a_un.a_val = 0;

   core->accessMemory(Core::NONE, WRITE, stack_ptr_base, (char*) &auxiliary_vector_entry_null, sizeof(auxiliary_vector_entry_null));
   stack_ptr_base += sizeof(auxiliary_vector_entry_null);

   LOG_ASSERT_ERROR(stack_ptr_base <= stack_ptr_top, "stack_ptr_base = 0x%x, stack_ptr_top = 0x%x", stack_ptr_base, stack_ptr_top);

}

VOID copySpawnedThreadStackData(ADDRINT reg_esp)
{
   core_id_t core_id = PinConfig::getSingleton()->getCoreIDFromStackPtr(reg_esp);

   PinConfig::StackAttributes stack_attr;
   PinConfig::getSingleton()->getStackAttributesFromCoreID(core_id, stack_attr);

   ADDRINT stack_upper_limit = stack_attr.lower_limit + stack_attr.size;
   
   UInt32 num_bytes_to_copy = (UInt32) (stack_upper_limit - reg_esp);

   Core* core = Sim()->getCoreManager()->getCurrentCore();

   core->accessMemory(Core::NONE, WRITE, reg_esp, (char*) reg_esp, num_bytes_to_copy);

}

VOID allocateStackSpace()
{
   // Note that 1 core = 1 thread currently
   // We should probably get the amount of stack space per thread from a configuration parameter
   // Each process allocates whatever it is responsible for !!
   UInt32 stack_size_per_core = PinConfig::getSingleton()->getStackSizePerCore();
   UInt32 num_cores = Sim()->getConfig()->getNumLocalCores();
   IntPtr stack_base = PinConfig::getSingleton()->getStackLowerLimit();

   LOG_PRINT(stderr, "allocateStackSpace: stack_size_per_core = 0x%x\n", stack_size_per_core);
   LOG_PRINT(stderr, "allocateStackSpace: num_local_cores = %i\n", num_cores);
   LOG_PRINT(stderr, "allocateStackSpace: stack_base = 0x%x\n", stack_base);

   // TODO: Make sure that this is a multiple of the page size 
   
   // mmap() the total amount of memory needed for the stacks
   assert(mmap((void*) stack_base, stack_size_per_core * num_cores,  PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) == (void*) stack_base);
}

VOID SimPthreadAttrInitOtherAttr(pthread_attr_t *attr)
{
   LOG_PRINT ("In SimPthreadAttrInitOtherAttr");

   core_id_t core_id;
   
   ThreadSpawnRequest* req = Sim()->getThreadManager()->getThreadSpawnReq();

   if (req == NULL)
   {
      // This is the thread spawner
      core_id = Sim()->getConfig()->getCurrentThreadSpawnerCoreNum();
   }
   else
   {
      // This is an application thread
      core_id = req->core_id;
   }

   PinConfig::StackAttributes stack_attr;
   PinConfig::getSingleton()->getStackAttributesFromCoreID(core_id, stack_attr);

   pthread_attr_setstack(attr, (void*) stack_attr.lower_limit, stack_attr.size);

   LOG_PRINT ("Done with SimPthreadAttrInitOtherAttr");
}