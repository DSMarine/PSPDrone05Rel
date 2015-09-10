#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <oslib/oslib.h>
#include <Drone.h>
#include <arpa/inet.h>
#include "ARDroneGeneratedTypes.h"

PSP_MODULE_INFO("PSPDrone", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(12*1024);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Globals:
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int runningFlag = 1;
int drawMenu = 0;
int showjoypad=0;
int showfire=0;
int TicRoof=4; //30 cmds por segundo @ 60fps, segun el manual del drone.
int GameTic=0;
int PktId=0;
int HRESULT=0;
int Connected=0;
int Hover=0;
int LedAnim=0, MoveAnim=0;
float_or_int_t LedAnimFreq;
const float delta=0.7;
float mscale=0.5;

//#define DRONE_IP "192.168.3.6"
#define DRONE_IP "192.168.1.1"

#define FTP_PORT				5551
#define AUTH_PORT				5552
#define VIDEO_RECORDER_PORT     5553
#define NAVDATA_PORT            5554
#define VIDEO_PORT              5555
#define AT_PORT                 5556
#define RAW_CAPTURE_PORT        5557
#define PRINTF_PORT             5558
#define CONTROL_PORT            5559

struct sockaddr_in dest;
char netbuf[1024];
unsigned int AT_SEQ=0;
int DroneSck;
int DroneUdp;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks:
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Exit callback */
int exit_callback(int arg1, int arg2, void *common) {
    runningFlag = 0;
    return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp) {
    int cbid;

    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void) {
    int thid = 0;
    thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
    if(thid >= 0)
        sceKernelStartThread(thid, 0, 0);
    return thid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Init OSLib:
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int initOSLib(){
    oslInit(0);
    oslInitGfx(OSL_PF_8888, 1); //16M Colors
    //oslInitGfx(OSL_PF_5650, 1);  //65K Colors
    oslInitAudio();
    oslSetQuitOnLoadFailure(1);
    oslSetKeyAutorepeatInit(40);
    oslSetKeyAutorepeatInterval(10);
    oslSetFrameskip(1);
    oslSetMaxFrameskip(4);
    return 0;
}

void FirePalette(OSL_IMAGE *img1,int alpha){
	int i;
    img1->palette = oslCreatePaletteEx(256, OSL_IN_RAM, OSL_PF_8888);
    u32 *paletteData = (u32*)img1->palette->data;
//    for (i=0;i<img1->palette->nElements;i++)
//        paletteData[i] = RGBA(i, i, i, 255);
    for (i=0;i<16;i++)
    	paletteData[i] = RGBA(0, 0, (i+1)*4, alpha);
    for (i=16;i<80;i++)
    	paletteData[i] = RGBA(((i-15)*4-1), 0, (79-i),alpha);
    for (i=80;i<208;i++)
    	paletteData[i] = RGBA(255, ((i-79)*2)-1, 0, alpha);
    for (i=208;i<256;i++)
    	paletteData[i] = RGBA(255, 255, (i-207)*5, alpha);

}

void JoyPadGfx(OSL_IMAGE *img1){
    static int jx;int jy,i,v;
    static int np;
    static u8 *rawdata;

    //oslLockImage(img1);
    for (i=0;i<130;i++){
    	rawdata=(u8*)oslGetImageLine(img1, i);
    	for (v=0;v<130;v++){
    		np=*rawdata;
    		if (np>2){
    			np=np-3;
    			*rawdata=np;
    		}
       		rawdata++;
    	}
    }
    jx=1+((osl_pad.analogX+128)/2);
    jy=1+((osl_pad.analogY+128)/2);
	rawdata=(u8*)oslGetImageLine(img1, jy);
	*(rawdata+jx-1)=255;
	*(rawdata+jx)=255;
	*(rawdata+jx+1)=255;
	rawdata=(u8*)oslGetImageLine(img1, jy-1);
	*(rawdata+jx)=255;
	rawdata=(u8*)oslGetImageLine(img1, jy+1);
	*(rawdata+jx)=255;
    //oslUnlockImage(img1);

	//oslDrawStringf(30, 50, "L1= %d",(int)oslGetImageLine(img1, 1));
	//oslDrawStringf(30, 70, "L2= %d",(int)oslGetImageLine(img1, 2));
	//oslDrawStringf(30, 90, "L3= %d",(int)oslGetImageLine(img1, 3));
	//remember to draw image at main!!
}

void FireFx(OSL_IMAGE *img1){
    int x,y,np;
    const int decay=1;
    u8 *rawdata1;
    u8 *rawdata2;

    //oslLockImage(img1);
	rawdata1=(u8*)oslGetImageLine(img1, 129);
    for (x=1;x<128;x++){
    	np=oslRandf(0,1)*255;
    	*rawdata1=np;
    	rawdata1++;
    }

    for (y=1;y<130;y++){
    	rawdata1=(u8*)oslGetImageLine(img1, y);
    	rawdata2=(u8*)oslGetImageLine(img1, y+1);
    	for (x=1;x<129;x++){
    		np=(*(rawdata1+x-1) + *(rawdata1+x) + *(rawdata1+x+1) + *(rawdata2+x))/4;
    		np=np-decay;
    		if (np<0) np=0;
        	rawdata2=(u8*)oslGetImageLine(img1, y-1);
    		*(rawdata2+x)=np;
    		}
    	}

		//Remember to draw image at main!!
}

int make_udpsocket(uint16_t port)
{
	int sock;
	int ret;
	struct sockaddr_in name;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock < 0)
	{
		return -1;
	}

	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	dest.sin_addr.s_addr = sceNetInetInetAddr(DRONE_IP);
	ret = bind(sock, (struct sockaddr *) &name, sizeof(name));
	if(ret < 0)
	{
		return -1;
	}
	return sock;
}

void Menu(){
    static char message[100] = "";
    static int dialog = OSL_DIALOG_NONE;

    dialog = oslGetDialogType();
    if (dialog){
        oslDrawDialog();
        if (oslGetDialogStatus() == PSP_UTILITY_DIALOG_NONE){
            if (oslDialogGetResult() == OSL_DIALOG_CANCEL)
                sprintf(message, "Cancel");
            else if (dialog == OSL_DIALOG_MESSAGE){
                int button = oslGetDialogButtonPressed();
                if (button == PSP_UTILITY_MSGDIALOG_RESULT_YES)
                    sprintf(message, "You pressed YES");
                else if (button == PSP_UTILITY_MSGDIALOG_RESULT_NO)
                    sprintf(message, "You pressed NO");
                else sprintf(message, "IDK what was pressed");
			}
            oslEndDialog();
        }
    }
    if (dialog == OSL_DIALOG_NONE){
        //oslReadKeys();
        if (osl_pad.held.triangle && osl_pad.held.down){
            runningFlag = 0;
        }else if (osl_pad.pressed.square){
            //oslInitMessageDialog("Test message dialog", 1);
            //memset(message, 0, sizeof(message));
            oslInitNetDialog();
            oslDialogDrawAndWait(OSL_DIALOG_NETCONF);
            HRESULT=oslDialogGetResult();
            if (HRESULT==OSL_DIALOG_CANCEL) {
            	sprintf(message, "Connection canceled(%d)",HRESULT);}
            	else {
            	sprintf(message, "Connection OK(%d)",HRESULT);
    			Connected = 1;
    			showjoypad=1;showfire=1;
    			drawMenu=0;
                snprintf(netbuf,1024,"AT*COMWDG=%d%c",AT_SEQ++,CR);//XY
    			sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));
    		    //Connected = oslNetSocketConnect(DroneSck, DRONE_IP, FTP_PORT);//For V1 TCP?
    		    //sprintf(message, "Connect: %d",Connected);
				//HResult = oslNetSocketSend(DroneSck, "TEST", 4);
            }
            oslEndDialog();
            //memset(message, 0, sizeof(message));
        }else if (osl_pad.pressed.start){
			drawMenu=0;
        }else if (osl_pad.pressed.select){
            //oslInitErrorDialog(0x80020001);
            //memset(message, 0, sizeof(message));
			snprintf(netbuf,1024,"AT*REF=%d,%d%c",AT_SEQ++,AT_REF|256,CR);//NO EMERGENCY
			sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));
		}else if (osl_pad.held.up && osl_pad.held.cross){
			runningFlag=0;
		}else if (osl_pad.pressed.up){
			if (mscale<1) mscale=mscale+0.05;
		}else if (osl_pad.pressed.down){
			if (mscale>0) mscale=mscale-0.05;
		}else if (osl_pad.pressed.left){
			snprintf(netbuf,1024,"AT*LED=%d,%d,%d,2%c",AT_SEQ++,LedAnim++,LedAnimFreq.i,CR);
			sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));
			if (LedAnim == ARDRONE_LED_NB_ANIMATION) LedAnim=0;
			//Drone.AddCmd "AT*LED=", CStr(Seq) & "," & CStr(Single2Long(2)) & ",2" vbcode
		}else if (osl_pad.pressed.right){
			snprintf(netbuf,1024,"AT*ANIM=%d,%d,%d%c",AT_SEQ++,MoveAnim,MAYDAY_TIMEOUT[MoveAnim],CR);
			MoveAnim++;
			sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));
			if (MoveAnim == ARDRONE_NB_ANIM_MAYDAY) MoveAnim=0;
		}
        oslDrawString(30, 50, "[] - Connect to ARDrone");
        oslDrawStringf(30, 70, "Left - LED animation %d/%d",LedAnim,ARDRONE_LED_NB_ANIMATION);
        oslDrawStringf(30, 90, "Right - Move animation %d/%d",MoveAnim,ARDRONE_NB_ANIM_MAYDAY);
        oslDrawString(30, 110, "Up/Down Change mov. scale");
        oslDrawString(30, 130, "Select - Emergency cutoff");
        oslDrawString(30, 150, "Start - Exit menu");
        oslDrawString(30, 170, "Up+X - Exit program");

        oslDrawString(30, 200, message);

    }

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main:
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int main(){
    SetupCallbacks();
    oslSrand(1);
    //Init network (for net conf dialog):
    oslNetInit();
    int skip = 0;
    LedAnimFreq.f=2;
    const int calx1=-20,calx2=20,caly1=-20,caly2=20;
    //float fx,fy;
    float_or_int_t _movW,_movX,_movY,_movZ;

    DroneSck = oslNetSocketCreate();
    DroneUdp = make_udpsocket(AT_PORT);
    initOSLib();
    oslIntraFontInit(INTRAFONT_CACHE_MED);

    //Loads image:
    //OSL_IMAGE *bkg = oslLoadImageFileJPG("back1.jpg", OSL_IN_RAM | OSL_SWIZZLED, OSL_PF_8888);
    OSL_IMAGE *bkg = oslLoadImageFileJPG("back1.jpg", OSL_IN_RAM | OSL_SWIZZLED, OSL_PF_5650);
   	OSL_IMAGE *img1 = oslCreateImage (130,130,OSL_IN_RAM,OSL_PF_8BIT);
   	FirePalette (img1,255);
    oslClearImage (img1,0);

    //Load font:
    OSL_FONT *font = oslLoadFontFile("flash0:/font/ltn0.pgf");
    oslSetFont(font);

    while(runningFlag && !osl_quit){
        if (!skip){
            oslStartDrawing();
            oslDrawImageXY(bkg, 0, 0);

            oslReadKeys();
            _movX.f = ((float)osl_pad.analogX/128)*mscale;
            _movY.f = ((float)osl_pad.analogY/128)*mscale;
            _movZ.f = 0;
            _movW.f = 0;
            if ((osl_pad.analogX > calx1) && (osl_pad.analogX < calx2)) _movX.f = 0; //Deadzone X
            if ((osl_pad.analogY > caly1) && (osl_pad.analogY < caly2)) _movY.f = 0; //Deadzone Y
			if (osl_pad.held.triangle) _movZ.f = delta; //Up
			if (osl_pad.held.cross) _movZ.f = -delta; //Down
			if (osl_pad.held.L) _movW.f = -delta; //CW
			if (osl_pad.held.R) _movW.f = delta; //CCW
            if (_movX.f==0 && _movY.f==0 && _movZ.f==0 && _movW.f==0) Hover=0; else Hover=1;

            oslDrawStringf(320, 10, "Tic#= %d",AT_SEQ);
            oslDrawStringf(10, 220, "x= %d  y= %d  z= %d  w= %d  !Hvr= %d",osl_pad.analogX,osl_pad.analogY,_movZ.i,_movW.i,Hover);
            oslDrawStringf(10, 240, "xf= %1.2f  yf= %1.2f  Scl= %1.2f",_movX.f,_movY.f,mscale);

            if (Connected==1) GameTic++;

            if (drawMenu==1) {
            	Menu();}
            else {
    			if (osl_pad.pressed.up) {
    				//Drone.AddCmd "AT*ANIM=", CStr(Seq) & "," & Anim_Time(Seq) vb anim code
    				snprintf(netbuf,1024,"AT*ANIM=%d,%d,%d%c",AT_SEQ++,ARDRONE_ANIM_FLIP_AHEAD,MAYDAY_TIMEOUT[ARDRONE_ANIM_FLIP_AHEAD],CR);
    				sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
    			if (osl_pad.pressed.down) {
    				snprintf(netbuf,1024,"AT*ANIM=%d,%d,%d%c",AT_SEQ++,ARDRONE_ANIM_FLIP_BEHIND,MAYDAY_TIMEOUT[ARDRONE_ANIM_FLIP_BEHIND],CR);
    				sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
    			if (osl_pad.pressed.right) {
    				snprintf(netbuf,1024,"AT*ANIM=%d,%d,%d%c",AT_SEQ++,ARDRONE_ANIM_FLIP_RIGHT,MAYDAY_TIMEOUT[ARDRONE_ANIM_FLIP_RIGHT],CR);
    				sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
    			if (osl_pad.pressed.left) {
    				snprintf(netbuf,1024,"AT*ANIM=%d,%d,%d%c",AT_SEQ++,ARDRONE_ANIM_FLIP_LEFT,MAYDAY_TIMEOUT[ARDRONE_ANIM_FLIP_LEFT],CR);
    				sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
                if (osl_pad.pressed.start) {
                	drawMenu=1;}
				if (osl_pad.pressed.circle){//Take off
					snprintf(netbuf,1024,"AT*REF=%d,%d%c",AT_SEQ++,AT_REF|512,CR);//TAKE OFF
					sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
				if (osl_pad.pressed.square){//Land
					snprintf(netbuf,1024,"AT*REF=%d,%d%c",AT_SEQ++,AT_REF,CR);
					sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}
            }
            if (showjoypad) JoyPadGfx(img1);
            if (showfire) FireFx(img1);
        	if (showjoypad||showfire) oslDrawImageXY(img1, 345, 85);

        	oslEndDrawing();

		}

		if (GameTic >= TicRoof){
        	GameTic=0;
            //snprintf(netbuf,1024,"AT*COMWDG=%d%cAT*PCMD=%d,%d,%d,%d,%d,%d%c",AT_SEQ++,CR,AT_SEQ++,Hover,_movX.i,_movY.i,_movZ.i,_movW.i,CR);//XY
            snprintf(netbuf,1024,"AT*PCMD=%d,%d,%d,%d,%d,%d%c",AT_SEQ++,Hover,_movX.i,_movY.i,_movZ.i,_movW.i,CR);//XY
			sendto(DroneUdp, netbuf, strlen(netbuf), 0, (struct sockaddr*)&dest, sizeof(dest));}


        oslEndFrame();
        skip = oslSyncFrame();
    }
    //Quit OSL:
    oslNetTerm();
    oslEndGfx();
    oslQuit();

    sceKernelExitGame();
    return 0;

}
