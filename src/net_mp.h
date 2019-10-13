// net_mpath.h

int  MPATH_Init (void);
void MPATH_Shutdown (void);
void MPATH_Listen (qboolean state);
int  MPATH_OpenSocket (int port);
int  MPATH_CloseSocket (int socket);
int  MPATH_Connect (int socket, struct qsockaddr *addr);
int  MPATH_CheckNewConnections (void);
int  MPATH_Read (int socket, byte *buf, int len, struct qsockaddr *addr);
int  MPATH_Write (int socket, byte *buf, int len, struct qsockaddr *addr);
int  MPATH_Broadcast (int socket, byte *buf, int len);
char *MPATH_AddrToString (struct qsockaddr *addr);
int  MPATH_StringToAddr (char *string, struct qsockaddr *addr);
int  MPATH_GetSocketAddr (int socket, struct qsockaddr *addr);
int  MPATH_GetNameFromAddr (struct qsockaddr *addr, char *name);
int  MPATH_GetAddrFromName (char *name, struct qsockaddr *addr);
int  MPATH_AddrCompare (struct qsockaddr *addr1, struct qsockaddr *addr2);
int  MPATH_GetSocketPort (struct qsockaddr *addr);
int  MPATH_SetSocketPort (struct qsockaddr *addr, int port);
