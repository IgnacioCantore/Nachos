#include "syscall.h"


int
main(int argc, char ** argv)
{
    if (argc != 2) {
        Write("Error: incorrect amount of arguments.\n", 38, 1);
        return -1;
    }
    
    OpenFileId src = Open(argv[1]);
    
    if (src != -1) {
        char ch[1];
        
        while (Read(ch, 1, src))
            Write(ch, 1, 1);
            
        Close(src);
        
        return 0;
    }
    
    else 
        return -1;
}
