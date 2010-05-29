#ifndef _DEBUG_H_
#define _DEBUG_H_


#if DEBUG
#define MDB(x) if (machine->config->debug) {ErrorF x;}

#define MINFO(x) if (machine->config->debug>1) {ErrorF x;}
// do {if (machine->config->debug == TRUE )
// {printf("config is%d\n", machine->config->debug); printf x;} }
// while (0)
#define DB(x)     ErrorF x

#else  /* DEBUG */
#define DB(x) do { ; } while (0)
#define MDB(x) do { ; } while (0)
#endif /* DEBUG */


#endif
