
#include <PS2X_lib.h>

volatile long systick_ms;
volatile int flag;

#define pi 3.1415926
typedef struct {
    float L0;
    float L1;
    float L2;
    float L3;

    float servo_angle[6];   //0号到4号舵机的角度
    float servo_range[6];   //舵机角度范围
    int servo_pwm[6];       //0号到4号舵机的角度
}kinematics_t;
kinematics_t kinematics;
void setup_kinematics(float L0, float L1, float L2, float L3, kinematics_t *kinematics) {
    //放大10倍
    kinematics->L0 = L0*10;
    kinematics->L1 = L1*10;
    kinematics->L2 = L2*10;
    kinematics->L3 = L3*10;
}

int kinematics_analysis(float x, float y, float z, float Alpha, kinematics_t *kinematics) {
    float theta3, theta4, theta5, theta6;
    float l0, l1, l2, l3;
    float aaa, bbb, ccc, zf_flag;

    //放大10倍
    x = x*10;
    y = y*10;
    z = z*10;

    l0 = kinematics->L0;
    l1 = kinematics->L1;
    l2 = kinematics->L2;
    l3 = kinematics->L3;

    if(x == 0) {
        theta6 = 0.0;
    } else {
        theta6 = atan(x/y)*180.0/pi;
    }

    y = sqrt(x*x + y*y);//x y 斜边
    y = y-l3 * cos(Alpha*pi/180.0);
    z = z-l0-l3*sin(Alpha*pi/180.0);
    if(z < -l0) {
        return 1;
    }
    if(sqrt(y*y + z*z) > (l1+l2)) {
        return 2;
    }

    ccc = acos(y / sqrt(y * y + z * z));
    bbb = (y*y+z*z+l1*l1-l2*l2)/(2*l1*sqrt(y*y+z*z));
    if(bbb > 1 || bbb < -1) {
        return 3;
    }
    if (z < 0) {
        zf_flag = -1;
    } else {
        zf_flag = 1;
    }
    theta5 = ccc * zf_flag + acos(bbb);
    theta5 = theta5 * 180.0 / pi;
    if(theta5 > 180.0 || theta5 < 0.0) {
        return 4;
    }

    aaa = -(y*y+z*z-l1*l1-l2*l2)/(2*l1*l2);
    if (aaa > 1 || aaa < -1) {
        return 5;
    }
    theta4 = acos(aaa);
    theta4 = 180.0 - theta4 * 180.0 / pi ;
    if (theta4 > 135.0 || theta4 < -135.0) {
        return 6;
    }

    theta3 = Alpha - theta5 + theta4;
    if(theta3 > 90.0 || theta3 < -90.0) {
        return 7;
    }

    kinematics->servo_angle[0] = theta6;
    kinematics->servo_angle[1] = theta5-90;
    kinematics->servo_angle[2] = theta4;
    kinematics->servo_angle[3] = theta3;

    kinematics->servo_pwm[0] = (int)(1500-2000.0 * kinematics->servo_angle[0] / 270.0);
    kinematics->servo_pwm[1] = (int)(1500+2000.0 * kinematics->servo_angle[1] / 270.0);
    kinematics->servo_pwm[2] = (int)(1500+2000.0 * kinematics->servo_angle[2] / 270.0);
    kinematics->servo_pwm[3] = (int)(1500-2000.0 * kinematics->servo_angle[3] / 270.0);

    return 0;
}
int kinematics_move(float x, float y, float z, int mtime) {

    int i,j, mmin = 0, flag = 0;

    if(y < 0)return 0;
    //寻找最佳角度
    flag = 0;
    for(i=0;i>=-135;i--) {
        if(0 == kinematics_analysis(x,y,z,i,&kinematics)){
            if(i<mmin)mmin = i;
            flag = 1;
        }
    }

    //用3号舵机与水平最大的夹角作为最佳值
    if(flag) {
        kinematics_analysis(x,y,z,mmin,&kinematics);
        for(j=0;j<4;j++) {
            set_servo(j, kinematics.servo_pwm[j], mtime);
        }
        return 1;
    }

    return 0;
}

/*
修改库文件中的缓冲buf大小为：168
hardwarearduinoavrcoresarduinoHardwareSerial.h
把库文件中的发送 和 接收缓冲宏定义设置大一点，这里我们都改为168,改完后编译注意字节有没有增加，只有增加了才表明更改成功
#define SERIAL_TX_BUFFER_SIZE 168   //定义为168
#define SERIAL_RX_BUFFER_SIZE 168   //定义为168
补充：也可以使用我们的编译环境，已经修改过了buf的大小
*/
#include <Servo.h>          //声明调用Servo.h库
#include <winbondflash.h>  //flash调用的库

#define ACTION_SIZE 512
#define INFO_ADDR_SAVE_STR   (((8<<10)-4)<<10) //(8*1024-4)*1024    //eeprom_info结构体存储的位置
#define BIAS_ADDR_VERIFY  0 //偏差存储的地址
#define FLAG_VERIFY 0x38

#define  PIN_nled   13              //宏定义工作指示灯引脚
#define  PIN_beep   4               //蜂鸣器引脚定义

#define nled_on() {digitalWrite(PIN_nled, LOW);}
#define nled_off() {digitalWrite(PIN_nled, HIGH);}

#define beep_on() {digitalWrite(PIN_beep, HIGH);}
#define beep_off() {digitalWrite(PIN_beep, LOW);}

#define PRE_CMD_SIZE 64
#define SERVO_NUM 6
#define SERVO_TIME_PERIOD    20    //每隔20ms处理一次（累加）舵机的PWM增量

typedef struct {
  long myversion;
  long dj_record_num;
  byte pre_cmd[PRE_CMD_SIZE+1];
  int  dj_bias_pwm[SERVO_NUM+1];
}eeprom_info_t;
eeprom_info_t eeprom_info;

Servo myservo[SERVO_NUM];         //创建一个舵机类
char buffer[168];                 // 定义一个数组用来存储每小组动作组
byte uart_receive_buf[168];
byte servo_pin[SERVO_NUM] = {7, 3, 5, 6, 9 ,8}; //宏定义舵机控制引脚
String uart_receive_str = "";    //声明一个字符串数组
String uart_receive_str_bak = "";    //声明一个字符串数组
byte uart1_get_ok = 0, uart1_mode=0;
char cmd_return[64];
int uart_receive_str_len;
int zx_read_id = 0, zx_read_flag = 0, zx_read_value = 0;
byte flag_sync=0;
bool downLoad = false;
u32 downLoadSystickMs=0;

typedef struct {                  //舵机结构体变量声明
    unsigned int aim = 1500;      //舵机目标值
    float cur = 1500.0;           //舵机当前值
    unsigned  int time1 = 1000;   //舵机执行时间
    float inc= 0.0;               //舵机值增量，以20ms为周期
}duoji_struct;
duoji_struct servo_do[SERVO_NUM];           //用结构体变量声明一个舵机变量组

winbondFlashSPI mem;
int do_start_index, do_time, group_num_start, group_num_end, group_num_times;
char group_do_ok = 1;
long long action_time = 0;
void(* resetFunc) (void) = 0; //declare reset function at address 0

//对 a 数进行排序
void selection_sort(int *a, int len) {
    int i,j,mi,t;
    for(i=0;i<len-1;i++) {
        mi = i;
        for(j=i+1;j<len;j++) {
            if(a[mi] > a[j]) {
                mi = j;
            }
        }
        if(mi != i) {
            t = a[mi];
            a[mi] = a[i];
            a[i] = t;
        }
    }
}

//查询str是包含str2，并返回最后一个字符所在str的位置
u16 str_contain_str(u8 *str, u8 *str2) {
  u8 *str_temp, *str_temp2;
  str_temp = str;
  str_temp2 = str2;
  while(*str_temp) {
    if(*str_temp == *str_temp2) {
      while(*str_temp2) {
        if(*str_temp++ != *str_temp2++) {
          str_temp = str_temp - (str_temp2-str2) + 1;
          str_temp2 = str2;
          break;
        }
      }
      if(!*str_temp2) {
        return (str_temp-str);
      }
    } else {
      str_temp++;
    }
  }
  return 0;
}

//电机控制函数
void car_run(short speed_left, short speed_right) {
    sprintf(cmd_return, "#006P%04dT0000!#007P%04dT0000!", 1500+speed_left, 1500-speed_right);
    Serial.println((char *)cmd_return);
    sprintf(cmd_return, "#008P%04dT0000!#009P%04dT0000!", 1500+speed_left, 1500-speed_right);
    Serial.println((char *)cmd_return);
    sprintf(cmd_return, "#000P%04dT0000!", 1500-(speed_left-speed_right)/2);
    Serial.println((char *)cmd_return);
}


//led灯初始化
void setup_nled() {
    pinMode(PIN_nled,OUTPUT); //设置引脚为输出模式
    nled_off();
}

//蜂鸣器初始化
void setup_beep() {
    pinMode(PIN_beep,OUTPUT);
    beep_off();
}

//存储器初始化
void setup_w25q() {
    read_eeprom();
    if(eeprom_info.dj_bias_pwm[SERVO_NUM] != FLAG_VERIFY) {
        for(int i=0;i<SERVO_NUM; i++) {
            eeprom_info.dj_bias_pwm[i] = 0;
        }
        eeprom_info.dj_bias_pwm[SERVO_NUM] = FLAG_VERIFY;
    }
}

//六路舵机初始化
void setup_servo() {
    //蜷缩 "{G0002#000P1500T1500!#001P2200T1500!#002P2500T1500!#003P2000T1500!#004P1500T1500!#005P1500T1500!}",
    if(eeprom_info.pre_cmd[PRE_CMD_SIZE] != FLAG_VERIFY) {
        servo_do[0].aim = servo_do[0].cur = 1500 + eeprom_info.dj_bias_pwm[0];servo_do[0].inc=0;
        servo_do[1].aim = servo_do[1].cur = 1500 + eeprom_info.dj_bias_pwm[1];servo_do[1].inc=0;
        servo_do[2].aim = servo_do[2].cur = 1500 + eeprom_info.dj_bias_pwm[2];servo_do[2].inc=0;
        servo_do[3].aim = servo_do[3].cur = 1500 + eeprom_info.dj_bias_pwm[3];servo_do[3].inc=0;
        servo_do[4].aim = servo_do[4].cur = 1500 + eeprom_info.dj_bias_pwm[4];servo_do[4].inc=0;
        servo_do[5].aim = servo_do[5].cur = 1500 + eeprom_info.dj_bias_pwm[5];servo_do[5].inc=0;
    }

    for(byte i = 0; i < SERVO_NUM; i ++){
        myservo[i].attach(servo_pin[i]);   // 将10引脚与声明的舵机对象连接起来
        myservo[i].writeMicroseconds(servo_do[i].aim);
    }
}

//启动提示
void setup_start_pre_cmd() {
    if(eeprom_info.pre_cmd[PRE_CMD_SIZE] == FLAG_VERIFY) {
          parse_cmd(eeprom_info.pre_cmd);
    }
}

//功能介绍：LED灯闪烁，每秒闪烁一次
void loop_nled() {
    static u8 val = 0;
    static unsigned long systick_ms_bak = 0;
    if(millis() - systick_ms_bak > 500) {
      systick_ms_bak = millis();
      if(val) {
        nled_on();
      } else {
        nled_off();
      }
      val = !val;
    }
}

//舵机PWM增量处理函数，每隔SERVO_TIME_PERIOD毫秒处理一次，这样就实现了舵机的连续控制
void loop_servo() {
  static byte servo_monitor[SERVO_NUM] = {0};
  static long long systick_ms_bak = 0;
  if(millis() - systick_ms_bak > SERVO_TIME_PERIOD) {
      systick_ms_bak = millis();
      for(byte i=0; i<SERVO_NUM; i++) {
          if(servo_do[i].inc) {
              if(abs( servo_do[i].aim - servo_do[i].cur) <= abs (servo_do[i].inc) ) {
                   myservo[i].writeMicroseconds(servo_do[i].aim);
                   servo_do[i].cur = servo_do[i].aim;
                   servo_do[i].inc = 0;
              } else {
                     servo_do[i].cur +=  servo_do[i].inc;
                     myservo[i].writeMicroseconds((int)servo_do[i].cur);
              }
          } else {
          }
      }
  }
}

//解析串口接收到的字符串指令
void loop_uart(){
    if(uart1_get_ok) {
          //如果有同步标志直接返回，让同步函数处理
        if(flag_sync)return;
        //打印字符串，测试的时候可以用
        //转换成字符串数组
        uart_receive_str.toCharArray(uart_receive_buf, uart_receive_str.length()+1);
        uart_receive_str_len = uart_receive_str.length();
        if(uart1_mode == 1) {
            parse_cmd(uart_receive_buf);
        } else if(uart1_mode == 2 || uart1_mode == 3){
            parse_action(uart_receive_buf);
        } else if(uart1_mode == 4){
            save_action(uart_receive_buf);
        }
        uart1_get_ok = 0;
        uart1_mode = 0;
        uart_receive_str = "";
    }
    if(millis()-downLoadSystickMs>3000) {
        downLoad = false;
    }
}

//获取最大时间
int getMaxTime(char *str) {
   int i = 0, max_time = 0, tmp_time = 0;
   while(str[i]) {
      if(str[i] == 'T') {
          tmp_time = (str[i+1]-'0')*1000 + (str[i+2]-'0')*100 + (str[i+3]-'0')*10 + (str[i+4]-'0');
          if(tmp_time>max_time)max_time = tmp_time;
          i = i+4;
          continue;
      }
      i++;
   }
   return max_time;
}

//把eeprom_info写入到W25Q64_INFO_ADDR_SAVE_STR位置
void read_eeprom(void) {
    mem.begin(_W25Q64,SPI,SS);
    mem.read( INFO_ADDR_SAVE_STR, (char *)(&eeprom_info), sizeof(eeprom_info_t));
    mem.end();
}

//把eeprom_info写入到INFO_ADDR_SAVE_STR位置
void rewrite_eeprom(void) {
    mem.begin(_W25Q64,SPI,SS);
    mem.eraseSector(INFO_ADDR_SAVE_STR);
    mem.write( INFO_ADDR_SAVE_STR, (char *)(&eeprom_info), sizeof(eeprom_info_t));
    mem.end();
}

//存储动作组
void save_action(char *str) {
  long long action_index = 0, max_time;

  //预存命令处理
  if(str[1] == '$' && str[2] == '!') {
    eeprom_info.pre_cmd[PRE_CMD_SIZE] = 0;
    rewrite_eeprom();
    Serial.println("@CLEAR PRE_CMD OK!");
    return;
  } else if(str[1] == '$') {
    memset(eeprom_info.pre_cmd, 0, sizeof(eeprom_info.pre_cmd));
    strcpy((char *)eeprom_info.pre_cmd, (char *)str+1);
    eeprom_info.pre_cmd[strlen((char *)str) - 2] = '\0';
    eeprom_info.pre_cmd[PRE_CMD_SIZE] = FLAG_VERIFY;
    rewrite_eeprom();
    Serial.println("@SET PRE_CMD OK!");
    Serial.println((char *)eeprom_info.pre_cmd);
    return;
  }

  //<G0000#000P1500T1000!>
  if((str[1] == 'G') && (str[6] == '#')) {
      action_index = (str[2]-'0')*1000 + (str[3]-'0')*100 + (str[4]-'0')*10 + (str[5]-'0') ;
      if(action_index<10000) {
        str[0] = '{';
        str[uart_receive_str_len-1] = '}';
        mem.begin(_W25Q64,SPI,SS);
        if((action_index*ACTION_SIZE % 4096) == 0){
            mem.eraseSector(action_index*ACTION_SIZE);
        }
        max_time = getMaxTime(str);
        uart_receive_str_len = uart_receive_str_len+4;
        str[uart_receive_str_len-4] = max_time/1000 + '0';
        str[uart_receive_str_len-3] = max_time%1000/100 + '0';
        str[uart_receive_str_len-2] = max_time%100/10 + '0';
        str[uart_receive_str_len-1] = max_time%10 + '0';
        str[uart_receive_str_len] = '\0';
        mem.write(action_index*ACTION_SIZE, str, uart_receive_str_len);
        mem.end();
        Serial.println("A");
      }
  }
}

//解析串口动作
void parse_action(u8 *uart_receive_buf) {
    static unsigned int index, time1, pwm1, pwm2, i, len;//声明三个变量分别用来存储解析后的舵机序号，舵机执行时间，舵机PWM
    if((uart_receive_buf[0] == '#') && (uart_receive_buf[4] == 'P') && (uart_receive_buf[5] == '!')) {
        delay(500);
    }
    Serial.println((char *)uart_receive_buf);
    if(zx_read_flag) {
      //#001P1500! 回读处理
      if((uart_receive_buf[0] == '#') && (uart_receive_buf[4] == 'P') && (uart_receive_buf[9] == '!')) {
            index = (uart_receive_buf[1]-'0')*100 +  (uart_receive_buf[2]-'0')*10 +  (uart_receive_buf[3]-'0');
              if(index == zx_read_id) {
                  zx_read_flag = 0;
              zx_read_value = (uart_receive_buf[5]-'0')*1000 + (uart_receive_buf[6]-'0')*100 +  (uart_receive_buf[7]-'0')*10 +  (uart_receive_buf[8]-'0');
              }
        }
    //#001PSCK+100! 偏差处理
    } else if((uart_receive_buf[0] == '#') && (uart_receive_buf[4] == 'P') && (uart_receive_buf[5] == 'S') && (uart_receive_buf[6] == 'C') && (uart_receive_buf[7] == 'K')) {
        index = (uart_receive_buf[1]-'0')*100 +  (uart_receive_buf[2]-'0')*10 +  (uart_receive_buf[3]-'0');
        if(index < SERVO_NUM) {
            int bias_tmp = (uart_receive_buf[9]-'0')*100 +  (uart_receive_buf[10]-'0')*10 +  (uart_receive_buf[11]-'0');
            if(bias_tmp < 127) {

              if(uart_receive_buf[8] == '+') {
                  servo_do[index].cur = servo_do[index].cur-eeprom_info.dj_bias_pwm[index]+bias_tmp;
                  eeprom_info.dj_bias_pwm[index] = bias_tmp;
              } else if(uart_receive_buf[8] == '-') {
                  servo_do[index].cur = servo_do[index].cur-eeprom_info.dj_bias_pwm[index]-bias_tmp;
                  eeprom_info.dj_bias_pwm[index] = -bias_tmp;
              }
              servo_do[index].aim =  servo_do[index].cur;
              servo_do[index].inc =  0.001;
              rewrite_eeprom();
              //Serial.print("input bias:");
              //Serial.println(eeprom_info.dj_bias_pwm[index]);
           }
        }
    //停止处理
    } else if((uart_receive_buf[0] == '#') && (uart_receive_buf[4] == 'P') && (uart_receive_buf[5] == 'D') && (uart_receive_buf[6] == 'S') && (uart_receive_buf[7] == 'T')) {
        index = (uart_receive_buf[1]-'0')*100 +  (uart_receive_buf[2]-'0')*10 +  (uart_receive_buf[3]-'0');
        if(index < SERVO_NUM) {
              servo_do[index].inc =  0.001;
              servo_do[index].aim = servo_do[index].cur;
        }
    } else if((uart_receive_buf[0] == '#') || (uart_receive_buf[0] == '{')) {   //解析以“#”或者以“{”开头的指令
        len = strlen(uart_receive_buf);     //获取串口接收数据的长度
        index=0; pwm1=0; time1=0;           //3个参数初始化
        for(i = 0; i < len; i++) {          //
            if(uart_receive_buf[i] == '#') {        //判断是否为起始符“#”
                i++;                        //下一个字符
                while((uart_receive_buf[i] != 'P') && (i<len)) {     //判断是否为#之后P之前的数字字符
                    index = index*10 + (uart_receive_buf[i] - '0');  //记录P之前的数字
                    i++;
                }
                i--;                          //因为上面i多自增一次，所以要减去1个
            } else if(uart_receive_buf[i] == 'P') {   //检测是否为“P”
                i++;
                while((uart_receive_buf[i] != 'T') && (i<len)) {  //检测P之后T之前的数字字符并保存
                    pwm1 = pwm1*10 + (uart_receive_buf[i] - '0');
                    i++;
                }
                i--;
            } else if(uart_receive_buf[i] == 'T') {  //判断是否为“T”
                i++;
                while((uart_receive_buf[i] != '!') && (i<len)) {//检测T之后!之前的数字字符并保存
                    time1 = time1*10 + (uart_receive_buf[i] - '0'); //将T后面的数字保存
                    i++;
                }
                if(time1<SERVO_TIME_PERIOD)time1=SERVO_TIME_PERIOD;//很重要，防止被除数为0
                if((index == 255) && (pwm1 >= 500) && (pwm1 <= 2500) && (time1<10000)) {  //如果舵机号和PWM数值超出约定值则跳出不处理
                        for(int i=0;i<SERVO_NUM;i++) {
                            pwm2 = pwm1+eeprom_info.dj_bias_pwm[i];
                            if(pwm2 > 2500)pwm2 = 2500;
                            if(pwm2 < 500)pwm2 = 500;
                            servo_do[i].aim = pwm2; //舵机PWM赋值,加上偏差的值
                            servo_do[i].time1 = time1;      //舵机执行时间赋值
                            float pwm_err = servo_do[i].aim - servo_do[i].cur;
                            servo_do[i].inc = (pwm_err*1.00)/(time1/SERVO_TIME_PERIOD); //根据时间计算舵机PWM增量
                        }
                } else if((index >= SERVO_NUM) || (pwm1 > 2500) ||(pwm1 < 500)|| (time1>10000)) {  //如果舵机号和PWM数值超出约定值则跳出不处理
                } else {
                    servo_do[index].aim = pwm1+eeprom_info.dj_bias_pwm[index];; //舵机PWM赋值,加上偏差的值
                    if(servo_do[index].aim > 2500)servo_do[index].aim = 2500;
                    if(servo_do[index].aim < 500)servo_do[index].aim = 500;
                    servo_do[index].time1 = time1;      //舵机执行时间赋值
                    float pwm_err = servo_do[index].aim - servo_do[index].cur;
                    servo_do[index].inc = (pwm_err*1.00)/(time1/SERVO_TIME_PERIOD); //根据时间计算舵机PWM增量
                }
                index = pwm1 = time1 = 0;
            }
        }
    }
}

//解析串口指令
void parse_cmd(u8 *uart_receive_buf) {
    static u8 jxb_r_flag = 0;
    int int1,int2,int3,int4;
    u16 pos;
    if(pos = str_contain_str((char *)uart_receive_buf, "$DRS!"), pos) {
        Serial.println("$DRS!");
    } else if(pos = str_contain_str((char *)uart_receive_buf, "$RST!"), pos) {
        resetFunc();
    } else if(pos = str_contain_str((char *)uart_receive_buf, "$DST!"), pos) {
        Serial.println("#255PDST!");
        for(int i;i<SERVO_NUM;i++) {
          servo_do[i].aim = servo_do[i].cur;
        }
        group_do_ok  = 1;
        Serial.println("@DoStop!");
    }else if(pos = str_contain_str((char *)uart_receive_buf, "$DST:"), pos) {
      if(sscanf(uart_receive_buf, "$DST:%d!", &int1)) {
            servo_do[int1].aim = servo_do[int1].cur;
        }
      Serial.println("@DoStop!");
    } else if(sscanf(uart_receive_buf, "$DCR:%d,%d!", &int2, &int3)) {
        car_run(int2,int3);
    } else if(pos = str_contain_str((char *)uart_receive_buf, "$PTG:"), pos) {
        if(sscanf(uart_receive_buf, "$PTG:%d-%d!", &int1, &int2)) {
            Serial.println(F("Your Action:"));
            mem.begin(_W25Q64,SPI,SS);
          for(int i=int1; i<=int2; i++) {
            mem.read((unsigned long)i*ACTION_SIZE, uart_receive_buf, 168);
                if(uart_receive_buf[0] == '{' && uart_receive_buf[1] == 'G') {
                    Serial.println((char *)uart_receive_buf);
                } else {
                    sprintf(uart_receive_buf, "@NoGroup %d!", int1);
                Serial.println((char *)uart_receive_buf);
                }
                }
         }
         mem.end();
    } else if(pos = str_contain_str((char *)uart_receive_buf, "$DGT:"), pos) {
        if(sscanf(uart_receive_buf, "$DGT:%d-%d,%d!", &int1, &int2, &int3)) {
            group_num_start = int1;
            group_num_end = int2;
            group_num_times = int3;
            do_time = int3;
            do_start_index = int1;
              if(int1 == int2) {
                  do_group_once(int1);
              } else {
                  group_do_ok = 0;
              }
            }
    }
    else if(pos = str_contain_str((char *)uart_receive_buf, "$KMS:"), pos) {
        if(sscanf((char *)uart_receive_buf, "$KMS:%d,%d,%d,%d!", &int1, &int2, &int3, &int4)) {
            Serial.println("Try to find best pos:");
            if(kinematics_move(int1, int2, int3, int4)) {
                beep_on();delay(50);beep_off();delay(50);
            } else {
                beep_on();delay(50);beep_off();delay(50);
                beep_on();delay(50);beep_off();delay(50);
                Serial.println("Can not find best pos!!!");
            }
        }
    }
    else if(pos = str_contain_str((char *)uart_receive_buf, "$GETA!"), pos) {
        downLoad = true;
        downLoadSystickMs = millis();
        Serial.println(F("AAA"));
    }  else {

    }
}

//循环执行动作组
void loop_action() {
  static long long systick_ms_bak = 0;
    if(group_do_ok == 0) {
      //循环检测单个动作执行时间是否到位
      if(millis() - systick_ms_bak > action_time) {
        systick_ms_bak =  millis();
        //Serial.println(do_start_index);
            if(group_num_times != 0 && do_time == 0) {
           group_do_ok = 1;
          Serial.println("@GroupDone!");
          return;
        }
        do_group_once(do_start_index);
          if(group_num_start<group_num_end) {
            if(do_start_index == group_num_end) {
              do_start_index = group_num_start;
              if(group_num_times != 0) {
                do_time--;
              }
              return;
            }
            do_start_index++;
          } else {
            if(do_start_index == group_num_end) {
              do_start_index = group_num_start;
              if(group_num_times != 0) {
                do_time--;
              }
              return;
            }
            do_start_index--;
          }
      }
  } else {
      action_time = 10;
  }
}

//执行动作组单次
void do_group_once(int index) {
  static long long systick_ms_bak = 0;
  int len;
  //读取动作
  mem.begin(_W25Q64,SPI,SS);
  mem.read((unsigned long)index*ACTION_SIZE, uart_receive_buf, 168);

  //获取时间
  for(int i=0;i<168;i++) {
    if(uart_receive_buf[i] == '}') {
        action_time =  (uart_receive_buf[i+1]-'0')*1000 + (uart_receive_buf[i+2]-'0')*100 + (uart_receive_buf[i+3]-'0')*10 + (uart_receive_buf[i+4]-'0');
            break;
     }
    }
  parse_action(uart_receive_buf);
  mem.end();
}

//执行动作组多次 只适用于总线舵机
void do_groups_times(int st, int ed, int tm) {
    int myst = st, myed = ed, mytm = tm;
    if(tm<=0)return;
    while(mytm) {
        for(int i=myst;i<=myed;i++) {
            do_group_once(i);
            delay(action_time);
        }
        mytm --;
        if(mytm) {
            myst = st;
            myed = ed;
        }
    }
}
//串口中断
void serialEvent() {
    static char sbuf_bak;
    while(Serial.available())  {      //
        sbuf_bak = char(Serial.read());

        if(uart1_get_ok) return;
        if(uart1_mode == 0) {
          if(sbuf_bak == '<') {
            uart1_mode = 4;
            downLoadSystickMs = millis();
              downLoad = true;
          } else if(sbuf_bak == '{') {
            uart1_mode = 3;
          } else if(sbuf_bak == '$') {
            uart1_mode = 1;
          } else if(sbuf_bak == '#') {
            uart1_mode = 2;
          }
          uart_receive_str = "";
        }

        uart_receive_str  += sbuf_bak;

        if((uart1_mode == 4) && (sbuf_bak == '>')){
          uart1_get_ok = 1;
        } else if((uart1_mode == 3) && (sbuf_bak == '}')){
          uart1_get_ok = 1;
        } else if((uart1_mode == 1) && (sbuf_bak == '!')){
          uart_receive_str_bak = uart_receive_str;
          uart1_get_ok = 1;
        } else if((uart1_mode == 2) && (sbuf_bak == '!')){
          uart1_get_ok = 1;
        }

        if(uart_receive_str.length() >= 168) {
            uart_receive_str = "";
        }
    }
}

void set_servo(int mindex, int mpwm, int mtime) {
    servo_do[mindex].aim = mpwm;
    servo_do[mindex].time1 = mtime;
    servo_do[mindex].inc = (servo_do[mindex].aim -  servo_do[mindex].cur) / (servo_do[mindex].time1/20.000);
    sprintf((char *)cmd_return, "#%03dP%04dT%04d! ", mindex, mpwm, mtime);
    Serial.print(cmd_return);
    //Serial.println(kinematics.servo_angle[mindex]);
}

;
#define PS2_DAT 12
#define PS2_CMD A0
#define PS2_CS A3
#define PS2_CLK 11
PS2X ps2x;
void setup_PS2() {
  ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_CS, PS2_DAT, true, true);
  Serial.println("setup_PS2_success");
}

void loop_PS2() {
  if (millis() - systick_ms > 50) {
    systick_ms = millis();
    ps2x.read_gamepad();
    handle_button_press();
    handle_button_release();

  }
}

void handle_button_press() {
  if (ps2x.ButtonPressed(PSB_PAD_LEFT)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 0, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_PAD_RIGHT)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 0, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_PAD_UP)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 1, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_PAD_DOWN)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 1, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_TRIANGLE)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 2, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_CROSS)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 2, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_SQUARE)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 3, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_CIRCLE)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 3, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_L1)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 4, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_R1)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 4, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_L2)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 5, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_R2)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 5, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
  if (ps2x.ButtonPressed(PSB_START)) {
        sprintf(cmd_return, "#%03dP%04dT%04d!", 255, 1500, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
    return;


  }
}

char cmd_return_tmp[64];

void handle_button_release() {
  if (ps2x.ButtonReleased(PSB_PAD_LEFT)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",0); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_PAD_RIGHT)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",0); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_PAD_UP)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",1); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_PAD_DOWN)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",1); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_TRIANGLE)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",2); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_CROSS)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",2); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_SQUARE)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",3); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_CIRCLE)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",3); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_L1)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",4); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_R1)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",4); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_L2)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",5); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
  if (ps2x.ButtonReleased(PSB_R2)) {
    sprintf(cmd_return_tmp, "#%03dPDST!",5); //组合指令
    Serial.println(cmd_return_tmp); //解析停止指令
    return;


  }
}

//手势识别
#include <SoftwareSerial.h>
SoftwareSerial mySerial(A1, A2); //RX=1,TX=2
int handStopFlag;
volatile long hand_systick_ms;

void loop_hand(byte handBuffer[]) {
  if (handStopFlag == 0 && handle_hand_start(handBuffer)) {
    hand_systick_ms = millis();
    handStopFlag = 1;
  }
  /*
  if (handStopFlag == 1 && (millis() - hand_systick_ms > 500)) {
    handle_hand_stop();
    handStopFlag = 0;
    //Serial.println("执行1");
  }
  else {
  
    if (handStopFlag == 0 && handle_hand_start(handBuffer)) {
      hand_systick_ms = millis();
      handStopFlag = 1;
      //Serial.println("执行2");
    }
    //Serial.println("执行2失败");
  }
  */
}

int handle_hand_start(byte handBuffer[]) {
  int returnFlag = 0;
    //byte handBuffer[2];
    //handBuffer[0] = mySerial.read();
    //handBuffer[1] = mySerial.read();
    //Serial.println(handBuffer[0]);
    //Serial.println(handBuffer[1]);
  //L2 松开 R2 合住 L1 R1转
  //左挥01 右挥10
  //握拳00 yes11
    if (handBuffer[0] == 0) {
      //Serial.println("buffer[0] is 0");
      if (handBuffer[1] == 0) {
        //Serial.println("branch1: buffer[1] is 0");
        // 00 -> R2
        sprintf(cmd_return, "#%03dP%04dT%04d!", 5, 2400, 500); //解析动作组
        parse_action(cmd_return); //解析动作组
        returnFlag = 1;
      }
      else if (handBuffer[1] == 1) {
        //Serial.println("branch2: buffer[1] is 1");
        //01 -> L1
        sprintf(cmd_return, "#%03dP%04dT%04d!", 4, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
        returnFlag = 1;
      }
    }
    else if (handBuffer[0] == 1) {
      //Serial.println("buffer[0] is 1");
      if (handBuffer[1] == 0) {
        //Serial.println("branch3: buffer[1] is 0");
        //10 -> R1
        sprintf(cmd_return, "#%03dP%04dT%04d!", 4, 2400, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
        returnFlag = 1;
      }
      else if (handBuffer[1] == 1) {
        //Serial.println("branch4: buffer[1] is 1");
        //11 -> L2
        sprintf(cmd_return, "#%03dP%04dT%04d!", 5, 600, 2000); //解析动作组
        parse_action(cmd_return); //解析动作组
        returnFlag = 1;
      }
    }
    //mySerial.flush();
  //}
  return returnFlag;
}
void handle_hand_stop() {
  sprintf(cmd_return, "#%03dPDST!", 4); //解析停止动作组
  parse_action(cmd_return); //解析动作组
  sprintf(cmd_return, "#%03dPDST!", 5); //解析停止动作组
  parse_action(cmd_return); //解析动作组
}

//位置坐标->舵机命令
volatile int pos_systick_ms;

int Handpos[4];

int fun(char y)
{
    int x = y - '0';
    return x;
}


int Detect(String tar,int i,int j)
{
    int item = i;
    if ((j - i) != 14)
    {
        return 0;
    }
    else {
        i++;
        while (i < j) {
            if ((fun(tar[i]) <= 9) && (fun(tar[i]) >= 0))
                i++;
            else
            {
                return 0;
            }
            if ((i==item+5||i==item+9) && tar[i]==',')
                i++;
        }
    }
    return 1;
}

void Tran(String posbuffer, int pos[])
{
    int i = 0,j = 0;
    int bufferLength = posbuffer.length();
    while (1) {
        if(i > bufferLength){
            pos[3] = 0;
            return;
        }
        if (posbuffer[i] == '*')
        {
            j = i;
            while (1) {
                if(j > bufferLength){
                    pos[3] = 0;
                    return;
                }
                if (posbuffer[j] == '#')
                    break;
                else
                    j++;
            }
            break;
        }
        else{
            i++;
        }
    }
    if (Detect(posbuffer,i,j))
    {
        pos[3] = 1;
        i++;
        if (posbuffer[i] == '0')
            pos[0]= fun(posbuffer[i + 1]) * 100 + fun(posbuffer[i + 2]) * 10 + fun(posbuffer[i + 3]);
        else
            pos[0] = 0 - (fun(posbuffer[i + 1]) * 100 + fun(posbuffer[i + 2]) * 10 + fun(posbuffer[i + 3]));
        
        pos[1] = fun(posbuffer[i + 5]) * 100 + fun(posbuffer[i + 6]) * 10 + fun(posbuffer[i + 7]);
        
        if (posbuffer[i + 9] == '0')
            pos[2]= fun(posbuffer[i + 10]) * 100 + fun(posbuffer[i + 11]) * 10 + fun(posbuffer[i + 12]);
        else
            pos[2] = 0 - (fun(posbuffer[i + 10]) * 100 + fun(posbuffer[i + 11]) * 10 + fun(posbuffer[i + 12]));
        }
    else
    {
        pos[3] = 0;
    }
}


void loop_pos(String Recbuffer)
{
  Tran(Recbuffer,Handpos);

  if (Handpos[3] == 1 && (millis() - pos_systick_ms > 400)) {
    if(kinematics_move(Handpos[0], Handpos[1], Handpos[2], 1000)){
      Handpos[3] == 0;
    }
    pos_systick_ms = millis();
  }
}

void setup(){
  //kinematics 90mm 105mm 98mm 150mm
setup_kinematics(90, 105, 98, 150, &kinematics);

  Serial.begin(115200); //串口初始化
  setup_w25q();             //读取全局变量
  setup_nled();             //led灯闪烁初始化
  setup_beep();             //蜂鸣器初始化
  setup_start_pre_cmd();        //系统启动

  systick_ms = 0;
  flag = 0;
  setup_servo();    //舵机初始化
  delay(3000); //必要延时
  setup_start_pre_cmd(); //系统启动
  //setup_PS2(); A0 A3引脚会被手柄占用

  //手势识别初始化
  mySerial.begin(115200);
  handStopFlag = 0;
  hand_systick_ms = 0;
  
  //位置->动作初始化
  Handpos[3]=0;
  pos_systick_ms = 0;
  kinematics_move(0, 0, 0, 2000);
  delay(3000);
}

void loop(){
  if(downLoad){
     loop_nled();
     loop_uart();
     loop_action();
     return;
  }
  loop_nled();    //切换LED灯状态实现LED灯闪烁，复用了MOSI口
  loop_uart();    //解析串口接收到的字符串指令
  loop_action();  //循环执行是否需要读取数据执行动作组
  loop_servo();    //处理模拟舵机增量
  //loop_PS2();


  byte readBuffer[2];
  if(mySerial.available()){
      readBuffer[0] = mySerial.read();
      String Recbuffer = "";
      if(readBuffer[0] == '*'){
        Recbuffer += char(readBuffer[0]);
        while(mySerial.available()){
          Recbuffer += char(mySerial.read());
          delay(2);
        }
        loop_pos(Recbuffer);
      }
      else{
        readBuffer[1] = mySerial.read();
        loop_hand(readBuffer);
      }
      //clear the serial buffer
      while(mySerial.available()){
        Recbuffer += char(mySerial.read());
        delay(2);
      }
  }
  if (handStopFlag == 1 && (millis() - hand_systick_ms > 500)) {
    handle_hand_stop();
    handStopFlag = 0;
  }
}
