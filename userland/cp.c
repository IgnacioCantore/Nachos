#include "syscall.h"


int
main(int argc, char ** argv)
{
    if (argc != 3) {
        Write("Error: incorrect amount of arguments.\n", 38, 1);
        return -1;
    }
    
    OpenFileId src = Open(argv[1]);
    
    Create(argv[2]);
    OpenFileId dst = Open(argv[2]);
    
    if (src != -1 && dst != -1) {
        char ch[1];
        
        while (Read(ch, 1, src))
            Write(ch, 1, dst);
            
        Close(src);
        Close(dst);
        
        return 0;
    }
    
    else 
        return -1;
}
