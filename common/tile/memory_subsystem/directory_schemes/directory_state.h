#ifndef __DIRECTORY_STATE_H__
#define __DIRECTORY_STATE_H__

class DirectoryState
{
   public:
      enum Type
      {
         UNCACHED = 0,
         SHARED,
         OWNED,
         MODIFIED,
         NUM_DIRECTORY_STATES
      };
};

#endif /* __DIRECTORY_STATE_H__ */
