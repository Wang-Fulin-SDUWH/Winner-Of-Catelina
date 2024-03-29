#include "reg52.h"
#include "LCD1602.h"
#include "intrins.h"

#define N 10                //N为取样次数
#define FOSC 12000000L      //晶振为12M
#define BAUD 2400           //因为产品采用了12M晶振，因此串口通信波特率设置为2400.

typedef unsigned char BYTE;
typedef unsigned int WORD;//为了便于区分，以上两条typedef语句仅用于串口通信相关语句，与串口通信无关的变量和数组仍采用unsigned char。
unsigned char welcome[]={"welcome!"};
unsigned char Show0[6]={"Noise:"};
unsigned char Show1[5]={"dB"};

unsigned long code Countnum[100]={4,5,6,7,8,9,9.2,9.4,9.7,10,           //31-40
								10.3,10.7,11.1,11.5,12,12.5,13,13.5,14,14.6,            //41-50
								15.2,16,16.8,17.7,19,21,23.5,26,29,34,                  //51-60
								40,46,52,60,69,79,91,103,117,131,                       //61-70
								146,162,176,192,209,227,247,278,301,336,                //71-80
								363,392,424,458,494,534,577,623,673,726,                //81-90
								785,848,915,988,1068,1153,1245,1345,1453,1569,
								1694,1830,1976,2135,2305,2490,2689,2904,3137,3388,
								3659,3951,4268,4609,4978,5376,5806,6271,6772,7314,
								7899,8531,9214,9951,10747,11607,12536,13538,14622,15791};//声压值数组
//31dB对应的声压值是10，之后每增加1dB，声压为原来的112%。


unsigned char code dB[100][3]={"031","032","033","034","035","036","037","038","039","040",		//分贝值二维数组
                              "041","042","043","044","045","046","047","048","049","050",
                              "051","052","053","054","055","056","057","058","059","060",
                              "061","062","063","064","065","066","067","068","069","070",
                              "071","072","073","074","075","076","077","078","079","080",
                              "081","082","083","084","085","086","087","088","089","090",
							  "091","092","093","094","095","096","097","098","099","100",
							  "101","102","103","104","105","106","107","108","109","110",
							  "111","112","113","114","115","116","117","118","119","120",
								"121","122","123","124","125","126","127","128","129","130",
							  };	  

sbit beep=P2^2;             //控制蜂鸣器的引脚 
sbit key_1=P2^1;            //增大报警临界值按键
sbit key_2=P2^0;            //减小报警临界值按键
sbit bit9=P2^3;             //显示串口通信接收的数据（实际上本产品只用到了发送功能）
bit busy;                   //用于串口通信的busy变量。

unsigned long count,c;      					//count是储存声压值的变量，c是count对应的临时变量，每经过一次主循环后c被清零。
unsigned char t,z;          					//t是单位时间（不确定具体秒数），z是定时器0用作计数器所得的计数值。
unsigned char shu=0,ys;       					//shu决定报警临界值。
unsigned long timer1;	      					//timer1是一个不断自增的变量，以产生单位时间，从而把定时器1解放出来，给串口通信使用。
unsigned int danwei=0;								//danwei是取样监测中单位时间的累计次数，取值为0-N，N为取样次数					
unsigned int noise_sum=0, noise_i;		//noise_sum是取样数组的各元素和，noise_i是计算取样数组和所用的循环变量
unsigned long noise[N];               //noise[N]是取样数组


void SendData(BYTE dat);   //单片机发送数据
void SendString(char *s);  //单片机发送字符串

void UART_INIT()                    //串口通信初始化（定时器1的启动放在了主函数中，与定时器0同时启动）
{
    SCON = 0x50;                    //8位模式
    TH1 = TL1 = -(FOSC/12/32/BAUD); //定时器1装初值
    TR1 = 1;                        //开定时器1
    ES = 1;                         //开中断4
    EA = 1;                         //开总中断
}

void SendData(BYTE dat)
{
    while (busy);           //等待前一位数据发送出去
    busy = 1;
    SBUF = dat;             //把数据存到缓冲区
}

void SendString(char *s)
{
    while (*s)              
    {
        SendData(*s++);     //一位一位地发送字符
    }
}


void delayms(uint xms)		   //延时xms
{
	uint x,y;
	for(x=xms;x>0;x--)
		for(y=90;y>0;y--);
}

void key()
{
	if (key_1==0)
	{
		delayms(10);             //按键消抖
		if (key_1==0)
		{
			while(key_1==0);       //松开后按键效果实现
			if (shu<99) shu++;     //分贝值可调上限为160
		}
	}
	if (key_2==0)
	{
		delayms(10);
		if (key_2==0)
		{
			while(key_2==0);
			if (shu>0) shu--;      //分贝值可调下限为31
		}
	}

}

void alarm()
{
	ys++;
	if (ys>200)
	{
		ys=0;
		beep=!beep;
	}
}

 //////////////////////////////////////////主函数///////////////////////////////////////////////////////////
void main(void)
{
		unsigned int m,n;//m，n用于确定二维分贝值数组的每个元素。
    unsigned int i;  //i是判断声压值达到声压数组哪一位的循环变量
    delayms(1000);
	  TMOD=0X25;		   //T0外部计数 工作方式1    T1定时方式1
    EA=0;				     //关总中断（在LCD屏中写入必要元素前暂时关闭总中断，防止定时器0用作计数器产生的计数误差）
    TH0=0;
    TL0=0;           //定时器0装初值
    init_1602();     //1602液晶初始化（详情见头文件LCD1602.h）

	WRITE_LCD1602_COM(0x80+3);	      //在第一行第四位开始写入字符串"Noise:"	
  	for(i=0; i<6; i++)
		{
				WRITE_LCD1602_DAT(Show0[i]);
				delayms(5);
		}
 
    WRITE_LCD1602_COM(0x80+12);	   	//在第一行第13位写入字符串"dB"
    for(i=0;i<2;i++)
    {
				WRITE_LCD1602_DAT(Show1[i]);
        delayms(5);        
    }
    WRITE_LCD1602_COM(0xc0+3);	   	//在第二行第四位写入字符串"Limit:"
		WRITE_LCD1602_byte("Limit:");

    WRITE_LCD1602_COM(0xc0+12);	   	//在第二行第13位写入字符串"dB"
    for(i=0;i<2;i++)
    {
				WRITE_LCD1602_DAT(Show1[i]);
        delayms(5);        
    }		 
    TR0=1;			                    //T0开始计数
    UART_INIT();                    //注意：总中断，定时器1等在这里开启
		
		
		while(1)//主循环
    {
			timer1++;
			if(timer1==50)
			{
				t++;
        if(t==20)	                      //把单位时间设置为"timer1++语句执行一千次所需的时间，产品的效果也就是每个单位时间更新一次LCD屏
        {
            t=0;                        //单位时间变量清零
            TR0=0;		                  //T0停止计数
            c=z*65535+(TH0*256+TL0);		//将计数值，也就是声压值读取到临时变量c中
            TH0=0;						          //清空计数值
            TL0=0;					            //清空计数值
            TR0=1;					   	        //T0再次开始计数
						count=c;                    //声压值存入count
						c=0;                        //c清零
						z=0;                        //T0的溢出次数清零
						noise[danwei]=count;        //danwei指单位时间经过的次数
						if(danwei<=N-1)
						{
							danwei++;
						}
						else if(danwei==N)                  //danwei达到取样次数后开始计算数组所有值的和，作为取样检测数据决定是否启动蓝牙报警。
						{
							danwei=0;
							for(noise_i=0;noise_i<N;noise_i++)
							{
								noise_sum+=noise[noise_i];
							}
							if(noise_sum>=52*N)               //如果取样时间内的平均值达到63分贝，则发送蓝牙报警信号（为了便于展示，N暂且设为一个较小的数）
							{
								SendString("118 is Noisy!");    //发送到接受端设备"118 is noisy"，起到强制督促功能
							}
							noise_sum=0;                      //清空数组和
						}
        }
				timer1=0;                               //清空timer1，防止数据溢出
				WRITE_LCD1602_COM(0xc0+9);
				WRITE_LCD1602_DAT(0x30+(shu+31)/100%10);
				WRITE_LCD1602_DAT(0x30+(shu+31)/10%10);
				WRITE_LCD1602_DAT(0x30+(shu+31)%10);    //将通过按键设置的Limit值写入LCD屏，依次写入百位十位个位。
							
							i=0;                              //循环第一次开始时清空循环比较变量
							if(count==0)                      //如果定时器0没有计数则重新开始主循环
							continue;
							while(count>Countnum[i])          //被声压值超过的最大下标即为分贝值
							{
									i++;
							}
							m=i;                              //把最大下标值保存到m中
							i=0;                              //清空循环比较变量
							WRITE_LCD1602_COM(0x80+9);        //在实时监测到的noise处写入监测数据
							for(n=0; n<3; n++)
							{     
									WRITE_LCD1602_DAT(dB[m][n]);  //根据二维分贝值数组写入LCD屏
									delayms(5);
							}
					
					WRITE_LCD1602_COM(0xc0+9);
					WRITE_LCD1602_DAT(0x30+(shu+31)/100%10);
					WRITE_LCD1602_DAT(0x30+(shu+31)/10%10);
					WRITE_LCD1602_DAT(0x30+(shu+31)%10) ; //再次写入Limit值
			}
					
			key();//按键函数
			if(m>=shu)
			{					
				alarm();//蜂鸣器在噪声实际检测值超过临界值时报警
			}else
			{
				beep=1;//否则关掉蜂鸣器
			}
    }    
}

void Timer0() interrupt 1      //定时器0中断，每当定时器计数值超过65536就触发一次该中断。
{
	z++;
}

void Uart_Isr() interrupt 4    //每当外界噪声触发检测条件后触发串口通信中断。
{
    if (RI)                 //单片机接收数据
    {
        RI = 0;             //Clear receive interrupt flag
        P0 = SBUF;          //P0 show UART data
        bit9 = RB8;         //P2.3 show parity bit
    }
    if (TI)                 //单片机发送数据
    {
        TI = 0;             //Clear transmit interrupt flag
        busy = 0;           //Clear transmit busy flag
    }
}
