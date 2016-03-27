#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <winsock.h>
#include <LCUI_Build.h>
#include LC_LCUI_H
#include <stdint.h>

/*------------------------------ LANBattleRoom ----------------------------*/
#pragma comment(lib,"ws2_32.lib")

typedef struct LANBattleUserInfo_ {
	char name[256];
	char ip_addr[13];
} LANBattleUserInfo;

typedef struct LANBattleRoom_ {
	char name[32];			/**< 房间名 */
	LANBattleUserInfo host;		/**< 房主的信息 */
	LANBattleUserInfo other;	/**< 参与者的信息 */
	LCUI_BOOL connected;		/**< 参与者是否与房主建立了连接 */
} LANBattleRoom;

typedef struct LANBattleRoomRec_ {
	LANBattleRoom room;	/**< 房间信息 */
	int unexist_count;	/**< 当前时段内房间不存在的次数 */
} LANBattleRoomRec;

typedef struct  LANBattleGameRole_ {
	int host_role;
	int other_role;
} LANBattleGameRole;

#define UDP_PORT_NUM 3000

static LANBattleRoom my_lan_battle_room;
static LCUI_Thread lan_battle_host_th = 0;
static LCUI_Thread lan_battle_scan_th = 0;
static LCUI_Thread lan_battle_ip_th = 0;
static LCUI_Queue lan_battle_room_list;
char msgSendToIp[13];
LANBattleGameRole testLanGameRole;
int is_create_room = -1;
void Lan_InitRoomListUI(void);
void Lan_UpdateRoomList(LANBattleRoom room);
void Lan_UpdateGameRole(int role_id,int is_my);
void Lan_ShowRoleSelectWnd(void);
void Lan_startRecOtherIP(void *arg);
void Lan_JoinLanRoom(LCUI_Widget *widget,LCUI_WidgetEvent *unused);
void Lan_sendIpToHost();
/* 负责广播当前计算机创建的房间信息 */
static void LANBattle_HostRoomThread( void *arg )
{
	SOCKET sockListener;
	BOOL fBroadcast = TRUE;
	SOCKADDR_IN saUdpServ;

	sockListener = socket( PF_INET, SOCK_DGRAM, 0 );
	setsockopt( sockListener,SOL_SOCKET, SO_BROADCAST, (char*)&fBroadcast, sizeof(BOOL));
	saUdpServ.sin_family = AF_INET;
	saUdpServ.sin_addr.s_addr = INADDR_BROADCAST;
	saUdpServ.sin_port = htons(UDP_PORT_NUM);
	
	while(1) {
		sendto( sockListener, (char*)&my_lan_battle_room, sizeof(LANBattleRoom),
			0, (SOCKADDR*)&saUdpServ, sizeof(SOCKADDR_IN));
		printf( "[server] room name: %s, host name: %s, host ip: %s\n", 
			my_lan_battle_room.name, my_lan_battle_room.host.name,
			my_lan_battle_room.host.ip_addr );
		LCUI_MSleep(1000);
	}
	_LCUIThread_Exit(NULL);
	return;
}

static void RoomList_Update(void)
{
	int i, n;
	LANBattleRoomRec *room_rec;
	LCUI_Queue *room_list;

	room_list = &lan_battle_room_list;
	Queue_Lock( room_list );
	n = Queue_GetTotal( room_list );
	for(i=0; i<n; ++i) {
		room_rec = (LANBattleRoomRec*)Queue_Get( room_list, i );
		if( !room_rec ) {
			continue;
		}
		++room_rec->unexist_count;
		if( room_rec->unexist_count >= 40 ) {
			Queue_Delete( room_list, i );
			n = Queue_GetTotal( room_list );
			--i;
		}
	}
	Queue_Unlock( room_list );
}

/* 标记一个房间已存在，若房间列表中无该房间记录，则添加它 */
static void RoomList_MarkExist( LANBattleRoom  *room )
{
	int i, n;
	LANBattleRoomRec room_buff, *room_rec;
	LCUI_Queue *room_list;

	room_list = &lan_battle_room_list;
	Queue_Lock( room_list );
	n = Queue_GetTotal( room_list );
	for(i=0; i<n; ++n) {
		room_rec = (LANBattleRoomRec*)Queue_Get( room_list, i );
		if( !room_rec ) {
			continue;
		}
		if( strcmp( room->name, room_rec->room.name ) == 0
		 && strcmp( room->host.ip_addr, room_rec->room.host.ip_addr ) == 0 ) {
			 room_rec->unexist_count = 0;
			 break;
		}
	}
	if( i >= n ) {
		room_buff.unexist_count = 0;
		memcpy( &room_buff.room, room, sizeof(LANBattleRoom) );
		printf("Find room %s,%s , n = %d\n",room_buff.room.name,room_buff.room.host.ip_addr,n);
		Queue_Add( room_list, &room_buff );
		Lan_UpdateRoomList(room_buff.room);
	}
	Queue_Unlock( room_list );
}

/* 负责扫描当前局域网中的各个房间信息 */
static void LANBattle_ScanRoomThread( void *arg )
{
	int nSize, nbSize, timeout;
	LANBattleRoom room;
	SOCKET sockListener;
	SOCKADDR_IN sin, saClient;

	sockListener = socket(AF_INET, SOCK_DGRAM,0);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(UDP_PORT_NUM);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	
	timeout = 10;
	setsockopt( sockListener, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout) );

	if(bind( sockListener, (SOCKADDR FAR *)&sin, sizeof(sin))!=0) {
		printf("Can't bind socket to local port!Program stop.\n");
		closesocket( sockListener );
		LCUIThread_Exit(NULL);
		return;
	}

	while(1) {
		nSize = sizeof(SOCKADDR_IN);
		nbSize = recvfrom( sockListener, (char*)&room, sizeof(LANBattleRoom),
					0, (SOCKADDR FAR *)&saClient, &nSize);
		/* 更新房间列表 */
		RoomList_Update();
		if( nbSize == SOCKET_ERROR ) {
			LCUI_MSleep(20);
			continue;
		}
		/* 标记该房间存在 */
		RoomList_MarkExist( &room );
	}
	closesocket( sockListener );
	_LCUIThread_Exit(NULL);
	return;
}

int LANBattle_Init(void)
{
	WSADATA wsaData;
	if( WSAStartup(MAKEWORD( 1, 1 ), &wsaData ) !=0 ) {
		printf("Can't initiates windows socket! Program stop.\n");
		return -1;
	}
	Queue_Init( &lan_battle_room_list, sizeof(LANBattleRoomRec), NULL );
	return 0;
}

int LANBattle_Exit(void)
{
	return 0;
}

/* 开始扫描对战房间 */
void LANBattleRoom_StartScan(void)
{
	Lan_InitRoomListUI();
	is_create_room = 1;
	_LCUIThread_Create( &lan_battle_scan_th, LANBattle_ScanRoomThread, NULL );
}

/* 创建房间 */
int LANBattleRoom_Create( const char *room_name )
{
	struct hostent *host_ptr;
	LANBattleRoom *p_room;
	struct in_addr addr;
	if( lan_battle_host_th != 0 ) {
		return 0;
	}
	p_room = &my_lan_battle_room;
	host_ptr = gethostbyname("");
	addr = *(struct in_addr*)*host_ptr->h_addr_list;
	/* 保存本地主机名 */
	strcpy( p_room->host.name, host_ptr->h_name);
	/* 保存本地主机ip地址 */
	strcpy( p_room->host.ip_addr, inet_ntoa(addr) );
	/* 保持房间名称 */
	strcpy( p_room->name, room_name );
	/* 创建线程，以广播房间信息 */
	return _LCUIThread_Create( &lan_battle_host_th, LANBattle_HostRoomThread, NULL );
}

/* 退出房间 */
int LANBattleRoom_Exit(void)
{
	return 0;
}

void LANBattleRoom_GetList( LCUI_Queue *out_room_list )
{
	int i, n;
	LANBattleRoomRec *room_rec;
	LCUI_Queue *room_list;

	/* 清空房间列表 */
	while( Queue_Delete(out_room_list, 0) );
	
	room_list = &lan_battle_room_list;
	Queue_Lock( room_list );
	n = Queue_GetTotal( room_list );
	for(i=0; i<n; ++i) {
		room_rec = (LANBattleRoomRec*)Queue_Get( room_list, i );
		if( !room_rec ) {
			continue;
		}
		Queue_Add( out_room_list, &room_rec->room );
	}
	Queue_Unlock( room_list );
}


void RoomList_Init( LCUI_Queue *list )
{
	Queue_Init( list, sizeof(LANBattleRoom), NULL );
}

/* 加入房间 */
int LANBattleRoom_Join( LANBattleRoom* room_buff )
{
	return 0;
}
/*----------------------------- END LANBattleRoom ---------------------------*/

#include LC_WIDGET_H
#include LC_BUTTON_H

static LCUI_Widget *btn_create_room, *btn_join_room, *btn_search_room;
void btn_create_room_clicked();
void btn_search_room_clicked();
/* 初始化局域网对战 */
void Game_InitLANBattle(void)
{
	LANBattle_Init();
	testLanGameRole.host_role = -1;
	testLanGameRole.other_role = -1;
	btn_create_room = Widget_New("button");
	btn_join_room = Widget_New("button");
	btn_search_room = Widget_New("button");

	Widget_SetAutoSize( btn_create_room, FALSE, 0 );
	Widget_SetAutoSize( btn_join_room, FALSE, 0 );
	Widget_SetAutoSize( btn_search_room, FALSE, 0 );
	Widget_Resize( btn_create_room, Size(100,30) );
	Widget_Resize( btn_join_room, Size(100,30) );
	Widget_Resize( btn_search_room, Size(100,30) );
	Button_TextW( btn_create_room, L"创建房间" );
	Button_TextW( btn_join_room, L"加入房间" );
	Button_TextW( btn_search_room, L"寻找房间" );
	Widget_SetAlign( btn_create_room, ALIGN_BOTTOM_RIGHT, Pos(-10,-10) );
	Widget_SetAlign( btn_join_room, ALIGN_BOTTOM_RIGHT, Pos(-120,-10) );
	Widget_SetAlign( btn_search_room, ALIGN_BOTTOM_RIGHT, Pos(-230,-10) );
	Widget_Show( btn_create_room );
	Widget_Show( btn_join_room );
	Widget_Show( btn_search_room );

	Widget_ConnectEvent( btn_create_room, EVENT_CLICKED, btn_create_room_clicked );
	Widget_ConnectEvent( btn_search_room, EVENT_CLICKED, btn_search_room_clicked );
}

void btn_create_room_clicked(LCUI_Widget *widget,LCUI_WidgetEvent *unused)
{
	printf("--OnCreateRoomClicked--\n");
	Lan_ShowRoleSelectWnd();
	_LCUIThread_Create( &lan_battle_ip_th, Lan_startRecOtherIP, NULL );
	LANBattleRoom_Create("TestRoom");
}

void btn_search_room_clicked(LCUI_Widget *widget,LCUI_WidgetEvent *unused)
{
	printf("--OnSearchRoomClicked--\n");
	LANBattleRoom_StartScan();
}

/*---------------*/
int Game_GetRoleID(id)
{
	if(Game_GetMyID() == -1){
		return -1;
	}
	if(id == Game_GetMyID()){
		return testLanGameRole.host_role;
	}else{
		return testLanGameRole.other_role;
	}
}

int Game_GetMyID()
{

	return is_create_room;
}

//------------------------------------
static LCUI_Widget *label_room_name,*btn_room_join;
void Lan_InitRoomListUI(void)
{
	GameMenu_OnlyShowMainMenu();
	Game_HideMainUI();
	label_room_name = Widget_New("label");
	btn_room_join = Widget_New("button");
	Widget_SetAutoSize( btn_room_join, FALSE, 0 );
	Widget_Resize( btn_room_join, Size(50,30) );
	Button_TextW( btn_room_join, L"加入房间" );
	Widget_SetAlign(label_room_name,ALIGN_MIDDLE_CENTER,Pos(-50,0));
	Widget_SetAlign(btn_room_join,ALIGN_MIDDLE_CENTER,Pos(50,0));
	Label_TextW(label_room_name,L"null");
	Widget_Show(label_room_name);
	Widget_Show(btn_room_join);

	Widget_ConnectEvent( btn_room_join, EVENT_CLICKED, Lan_JoinLanRoom );
}
void Lan_UpdateRoomList(LANBattleRoom room)
{
	strcpy(msgSendToIp,room.host.ip_addr);
	Label_TextW(label_room_name,room.name);
}

void Lan_JoinLanRoom(LCUI_Widget *widget,LCUI_WidgetEvent *unused)
{
	is_create_room = 2;
	Lan_sendIpToHost();
	printf("2222222222222222222");
	Lan_ShowRoleSelectWnd();
	printf("33333333333333333333");
}

void Lan_UpdateGameRole(int role_id, int is_my)
{
	if(Game_GetMyID() == is_my){
		testLanGameRole.host_role = role_id;
	}else{
		testLanGameRole.other_role = role_id;
	}
}

void Lan_ShowRoleSelectWnd()
{
	Game_InitRoleSelectBox();
    Game_ShowRoleSelectBox();
}

void Lan_sendIpToHost()
{
	SOCKET sockListener;
	BOOL fBroadcast = TRUE;
	SOCKADDR_IN saUdpServ;

	sockListener = socket( PF_INET, SOCK_DGRAM, 0 );
	setsockopt( sockListener,SOL_SOCKET, SO_BROADCAST, (char*)&fBroadcast, sizeof(BOOL));
	saUdpServ.sin_family = AF_INET;
	saUdpServ.sin_addr.s_addr = inet_addr(msgSendToIp);
	saUdpServ.sin_port = htons(3001);

	char myIp[13];
	struct hostent *host_ptr;
	struct in_addr addr;
	host_ptr = gethostbyname("");
	addr = *(struct in_addr*)*host_ptr->h_addr_list;
	strcpy( myIp, inet_ntoa(addr) );


	sendto( sockListener, myIp, sizeof(msgSendToIp),
		0, (SOCKADDR*)&saUdpServ, sizeof(SOCKADDR_IN));
	printf("0-------0-----00");
	printf("--myIp--%s--\n",myIp);
	printf("1---------1------------1");
	return;
}

void Lan_startRecOtherIP(void *arg)
{
	int nSize, nbSize, timeout;
	LANBattleRoom room;
	SOCKET sockListener;
	SOCKADDR_IN sin, saClient;

	sockListener = socket(AF_INET, SOCK_DGRAM,0);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(3001);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	timeout = 10;
	setsockopt( sockListener, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout) );

	if(bind( sockListener, (SOCKADDR FAR *)&sin, sizeof(sin))!=0) {
		printf("Can't bind socket to local port!Program stop.\n");
		closesocket( sockListener );
		LCUIThread_Exit(NULL);
		return;
	}
	while(1) {
		nSize = sizeof(SOCKADDR_IN);
		nbSize = recvfrom( sockListener, (char*)msgSendToIp, sizeof(msgSendToIp),
					0, (SOCKADDR FAR *)&saClient, &nSize);
		if( nbSize == SOCKET_ERROR ) {
			LCUI_MSleep(20);
			continue;
		}
		printf("-otherIP = %s\n",msgSendToIp);
		closesocket( sockListener );
		_LCUIThread_Exit(NULL);
		return;
	}
}