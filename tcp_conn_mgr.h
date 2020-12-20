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

#define MAX_CONN_NO 5 /*Maximum supported concurrent connection by esp*/

/* Exported functions -------------------------------------------------------*/
struct espconn *TcpMgr_OpenConn(void);
void TcpMgr_CloseConn(struct espconn * pConn);

#endif