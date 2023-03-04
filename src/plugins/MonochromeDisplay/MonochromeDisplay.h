#ifndef __MONOCHROME_DISPLAY__
#define __MONOCHROME_DISPLAY__

#if defined(ENA_NOKIA) || defined(ENA_SSD1306)
#ifdef ENA_NOKIA
    #include <U8g2lib.h>
    #define DISP_PROGMEM U8X8_PROGMEM
#else // ENA_SSD1306
    /* esp8266 : SCL = 5, SDA = 4 */
    /* ewsp32  : SCL = 22, SDA = 21 */
    #include <Wire.h>
    #include <SSD1306Wire.h>
    #define DISP_PROGMEM PROGMEM
#endif

#include <Timezone.h>

#include "../../utils/helper.h"
#include "../../hm/hmSystem.h"

static uint8_t bmp_arrow[] DISP_PROGMEM = {
    B00000000, B00011100, B00011100, B00001110, B00001110, B11111110, B01111111,
    B01110000, B01110000, B00110000, B00111000, B00011000, B01111111, B00111111,
    B00011110, B00001110, B00000110, B00000000, B00000000, B00000000, B00000000};

static TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
static TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Tim

template<class HMSYSTEM>
class MonochromeDisplay {
    public:
        #if defined(ENA_NOKIA)
        MonochromeDisplay() : mDisplay(U8G2_R0, 5, 4, 16), mCE(CEST, CET) {
            mNewPayload = false;
            mExtra      = 0;
        }
        #else // ENA_SSD1306
        MonochromeDisplay() : mDisplay(0x3c, SDA, SCL), mCE(CEST, CET) {
            mNewPayload = false;
            mExtra      = 0;
            mRx         = 0;
            mUp         = 1;
        }
        #endif

        void setup(HMSYSTEM *sys, uint32_t *utcTs) {
            mSys   = sys;
            mUtcTs = utcTs;
            memset( mToday, 0, sizeof(float)*MAX_NUM_INVERTERS );
            memset( mTotal, 0, sizeof(float)*MAX_NUM_INVERTERS );
            mLastHour = 25;
            #if defined(ENA_NOKIA)
                mDisplay.begin();
                ShowInfoText("booting...");
            #else
                mDisplay.init();
                mDisplay.flipScreenVertically();
                mDisplay.setContrast(63);
                mDisplay.setBrightness(63);

                mDisplay.clear();
                mDisplay.setFont(ArialMT_Plain_24);
                mDisplay.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);

                mDisplay.drawString(64,22,"Starting...");
                mDisplay.display();
                mDisplay.setTextAlignment(TEXT_ALIGN_LEFT);
            #endif
        }

        void loop(void) {

        }

        void payloadEventListener(uint8_t cmd) {
            mNewPayload = true;
        }

        void tickerSecond() {
            static int cnt=1;
            if(mNewPayload || !(cnt % 10)) {
                cnt=1;
                mNewPayload = false;
                DataScreen();
            }
            else
               cnt++;
        }

    private:
        #if defined(ENA_NOKIA)
        void ShowInfoText(const char *txt) {
            /* u8g2_font_open_iconic_embedded_2x_t 'D' + 'G' + 'J' */
            mDisplay.clear();
            mDisplay.firstPage();
            do {
                const char *e;
                const char *p = txt;
                int y=10;
                mDisplay.setFont(u8g2_font_5x8_tr);
                while(1) {
                    for(e=p+1; (*e && (*e != '\n')); e++);
                    size_t len=e-p;
                    mDisplay.setCursor(2,y);
                    String res=((String)p).substring(0,len);
                    mDisplay.print(res);
                    if ( !*e )
                        break;
                    p=e+1;
                    y+=12;
                }
                mDisplay.sendBuffer();
            } while( mDisplay.nextPage() );
        }
        #endif

        void DataScreen(void) {
            String timeStr = ah::getDateTimeStr(mCE.toLocal(*mUtcTs)).substring(2, 22);
            int hr = timeStr.substring(9,11).toInt();
            IPAddress ip = WiFi.localIP();
            float totalYield = 0.0, totalYieldToday = 0.0, totalActual = 0.0;
            char fmtText[32];
            int  ucnt=0, num_inv=0;
            unsigned int pow_i[ MAX_NUM_INVERTERS ];

            memset( pow_i, 0, sizeof(unsigned int)* MAX_NUM_INVERTERS );
            if ( hr < mLastHour )  // next day ? reset today-values
            {
                memset( mToday, 0, sizeof(float)*MAX_NUM_INVERTERS );
                memset( yDayPanel, 0, sizeof(int)*4);
            }
            mLastHour = hr;

            for (uint8_t id = 0; id < mSys->getNumInverters(); id++) {
                Inverter<> *iv = mSys->getInverterByPos(id);
                if ( num_inv == 2 )  /* max 2 inverters */
                   break;
                if (NULL != iv) {
                    record_t<> *rec = iv->getRecordStruct(RealTimeRunData_Debug);
                    uint8_t pos;
                    uint8_t list[] = {FLD_PAC, FLD_YT, FLD_YD};

                    int isprod = iv->isProducing(*mUtcTs,rec);

                    for(uint8_t i = 1; ( i <= iv->channels ) && (i<=2); i++)
                    { /* get 1st two panel values */
                        float today_this;
                        pos = iv->getPosByChFld(i, FLD_YD, rec);
                        today_this = isprod ? iv->getValue(pos, rec) : 0;
                        if ( today_this > yDayPanel[num_inv*2+i-1] )
                            yDayPanel[num_inv*2+i-1] = today_this;
                        pos = iv->getPosByChFld(i, FLD_PDC, rec);
                        yPowPanel[num_inv*2+i-1] = isprod ? iv->getValue(pos, rec) : 0;
                    }

                    for (uint8_t fld = 0; fld < 3; fld++) {
                        pos = iv->getPosByChFld(CH0, list[fld],rec);

                        if(fld == 1)
                        {
                            float total_this = iv->getValue(pos,rec);
                            if ( isprod && (total_this > mTotal[num_inv]))
                                mTotal[num_inv] = total_this;
                            totalYield += mTotal[num_inv];
                        }
                        if(fld == 2)
                        {
                            float today_this = iv->getValue(pos,rec);
                            if ( isprod && (today_this > mToday[num_inv]))
                                mToday[num_inv] = today_this;
                            totalYieldToday += mToday[num_inv];
                        }
                        if((fld == 0) && isprod )
                        {
                            pow_i[num_inv] = iv->getValue(pos,rec);
                            totalActual += iv->getValue(pos,rec);
                            ucnt++;
                        }
                    }
                    num_inv++;
                }
            }
            /* u8g2_font_open_iconic_embedded_2x_t 'D' + 'G' + 'J' */
            mDisplay.clear();
#if defined(ENA_NOKIA)
                mDisplay.firstPage();
                do {
                    if(ucnt) {
                        mDisplay.drawXBMP(10,1,8,17,bmp_arrow);
                        mDisplay.setFont(u8g2_font_logisoso16_tr);
                        mDisplay.setCursor(25,17);
                        sprintf(fmtText,"%3.0f",totalActual);
                        mDisplay.print(String(fmtText)+F(" W"));
                    }
                    else
                    {
                        mDisplay.setFont(u8g2_font_logisoso16_tr  );
                        mDisplay.setCursor(10,17);
                        mDisplay.print(String(F("offline")));
                    }
                    mDisplay.drawHLine(2,20,78);
                    mDisplay.setFont(u8g2_font_5x8_tr);

                    if (( num_inv < 2 ) || !(mExtra%2))
                    {
                        mDisplay.setCursor(5+mExtra%2,29);
                        sprintf(fmtText,"%4.0f",totalYieldToday);
                        mDisplay.print(F("today ")+String(fmtText)+F(" Wh"));
                        mDisplay.setCursor(5+mExtra%2,37);
                        sprintf(fmtText,"%.1f",totalYield);
                        mDisplay.print(F("total ")+String(fmtText)+F(" kWh"));
                    }
                    else
                    {
                        int id1=(mExtra/2)%(num_inv-1);
                        int yloo=(mExtra/num_inv)%2;
                        mDisplay.setCursor(3,29);
                        if ( yloo )
                        {
                            mDisplay.print(F(" "+String(yPowPanel[id1*2+0])+F(" W")));
                            mDisplay.setCursor(64,29);
                            mDisplay.print(F(" "+String(yPowPanel[id1*2+1])+F(" W")));
                            if ( id1 < (int)num_inv-2 )
                            {
                                mDisplay.setCursor(3,37);
                                mDisplay.print(F(" "+String(yPowPanel[id1*2+2])+F(" W")));
                                mDisplay.setCursor(64,37);
                                mDisplay.print(F(" "+String(yPowPanel[id1*2+3])+F(" W")));
                            }
                        }
                        else
                        {
                            sprintf(fmtText,"%.1f",yDayPanel[id1*2+0]);
                            mDisplay.print(F(""+String(fmtText)+F(" Wh")));
                            mDisplay.setCursor(64,29);
                            sprintf(fmtText,"%.1f",yDayPanel[id1*2+1]);
                            mDisplay.print(F(""+String(fmtText)+F(" Wh")));
                            if ( id1 < (int)num_inv-2 )
                            {
                                sprintf(fmtText,"%.1f",yDayPanel[id1*2+2]);
                                mDisplay.setCursor(3,37);
                                mDisplay.print(F(""+String(fmtText)+F(" Wh")));
                                mDisplay.setCursor(64,37);
                                sprintf(fmtText,"%.1f",yDayPanel[id1*2+3]);
                                mDisplay.print(F(""+String(fmtText)+F(" Wh")));
                            }
                        }
                    }
                    if ( !(mExtra%10) && ip ) {
                        mDisplay.setCursor(5,47);
                        mDisplay.print(ip.toString());
                    }
                    else {
                        mDisplay.setCursor(0,47);
                        mDisplay.print(timeStr);
                    }

                    mDisplay.sendBuffer();
                } while( mDisplay.nextPage() );
                mExtra++;
#else // ENA_SSD1306

            if(mUp) {
                mRx += 2;
                if(mRx >= 20)
                mUp = 0;
            } else {
                mRx -= 2;
                if(mRx <= 0)
                mUp = 1;
            }
            int ex = 2*( mExtra % 5 );

            if(ucnt) {
                mDisplay.setBrightness(63);
                mDisplay.drawXbm(10+ex,5,8,17,bmp_arrow);
                mDisplay.setFont(ArialMT_Plain_24);
                sprintf(fmtText,"%3.0f",totalActual);
                mDisplay.drawString(25+ex,0,String(fmtText)+F(" W"));
            }
            else
            {
                mDisplay.setBrightness(1);
                mDisplay.setFont(ArialMT_Plain_24);
                mDisplay.drawString(25+ex,0,String(F("offline")));
            }
            mDisplay.setFont(ArialMT_Plain_16);

            if (( num_inv !=2 ) || (phase==0))
            {
                sprintf(fmtText,"%4.0f",totalYieldToday);
                mDisplay.drawString(5,22,F("today ")+String(fmtText)+F(" Wh"));
                sprintf(fmtText,"%.1f",totalYield);
                mDisplay.drawString(5,35,F("total  ")+String(fmtText)+F(" kWh"));
            }
            else if ( phase == 1 )
            {
                mDisplay.drawString(5,22,F("1: ")+String(yPowPanel[0]));
                mDisplay.drawString(70,22,F("")+String(yPowPanel[1]));
                if ( num_inv > 1 )
                {
                    mDisplay.drawString(5,35,F("2: ")+String(yPowPanel[2]));
                    mDisplay.drawString(70,35,F("")+String(yPowPanel[3]));
                }
                mDisplay.drawString(100,49,F("W"));
                mDisplay.drawString(67,49,F("D"));
                mDisplay.drawString(31,49,F("Inv"));
                mDisplay.fillRect(4,51,20,14);
                mDisplay.setColor( BLACK );
                mDisplay.drawString(8,49,F("P"));
                mDisplay.setColor( WHITE );
            }
            else if ( phase == 2 )
            {
                if( pow_i[0] )
                    mDisplay.drawString(10,22,F("1: ")+String(pow_i[0]));
                else
                    mDisplay.drawString(10,22,F("1: -----"));
                if( pow_i[1] )
                    mDisplay.drawString(10,35,F("2: ")+String(pow_i[1]));
                else
                    mDisplay.drawString(10,35,F("2: -----"));
                mDisplay.drawString(100,49,F("W"));
                mDisplay.drawString(67,49,F("D"));
                mDisplay.drawString(8,49,F("P"));
                mDisplay.fillRect(28,51,28,14);
                mDisplay.setColor( BLACK );
                mDisplay.drawString(31,49,F("Inv"));
                mDisplay.setColor( WHITE );
            }
            else if ( phase == 3 )
            {
                sprintf(fmtText,"%.0f",yDayPanel[0]);
                mDisplay.drawString(3,22,F("1: ")+String(fmtText));
                sprintf(fmtText,"%.0f",yDayPanel[1]);
                mDisplay.drawString(70,22,F("")+String(fmtText));
                if ( num_inv > 1 )
                {
                    sprintf(fmtText,"%.0f",yDayPanel[2]);
                    mDisplay.drawString(3,35,F("2: ")+String(fmtText));
                    sprintf(fmtText,"%.0f",yDayPanel[3]);
                    mDisplay.drawString(70,35,F("")+String(fmtText));
                }
                mDisplay.drawString(100,49,F("Wh"));
                mDisplay.fillRect(4,51,20,14);
                mDisplay.fillRect(64,51,20,14);
                mDisplay.setColor( BLACK );
                mDisplay.drawString(8,49,F("P"));
                mDisplay.drawString(67,49,F("D"));
                mDisplay.setColor( WHITE );
                mDisplay.drawString(31,49,F("Inv"));
            }
            phase++;
            if ( phase == 4 )
                phase = 0;

            mDisplay.drawLine(2,23,123,23);

            if ( phase == 1 )
            {
                if ( (!((mExtra/4)%4) && ip )|| (timeStr.length()<16))
                {
                    mDisplay.drawString(5,49,ip.toString());
                }
                else
                {
                    int w=mDisplay.getStringWidth(timeStr.c_str(),timeStr.length(),0);
                    if ( 1 || w>127 )
                    {
                        String tt=timeStr.substring(9,17);
                        w=mDisplay.getStringWidth(tt.c_str(),tt.length(),0);
                        mDisplay.drawString(127-w-mRx,49,tt);
                    }
                    else
                        mDisplay.drawString(0,49,timeStr);
                }
            }

            mDisplay.display();
            mExtra++;
        #endif
        }

        // private member variables
        #if defined(ENA_NOKIA)
            U8G2_PCD8544_84X48_1_4W_HW_SPI mDisplay;
        #else // ENA_SSD1306
            SSD1306Wire mDisplay;
            int mRx;
            char mUp;
        #endif
        int mExtra;
        bool mNewPayload;
        float mTotal[ MAX_NUM_INVERTERS ];
        float mToday[ MAX_NUM_INVERTERS ];
        unsigned int yPowPanel[ 8 ];
        float        yDayPanel[ 8 ];
        int         phase;

        uint32_t *mUtcTs;
        int mLastHour;
        HMSYSTEM *mSys;
        Timezone mCE;
};
#endif

#endif /*__MONOCHROME_DISPLAY__*/
