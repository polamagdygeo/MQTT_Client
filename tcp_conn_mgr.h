/**
  ******************************************************************************
  * @file           : 
  * @version        :
  * @brief          :
  ******************************************************************************
  ******************************************************************************
*/

#ifndef __TCPCONN___
#define __TCPCONN___

#include "espconn.h"

/* Exported functions -------------------------------------------------------*/
struct espconn *TcpMgr_OpenConn(void);
void TcpMgr_CloseConn(struct espconn * pConn);

#endif