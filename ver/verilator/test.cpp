#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <string>
#include <list>
#include "Vtop.h"
#include "verilated_vcd_c.h"
#include "VGMParser.hpp"
#include "feature.hpp"

  // #include "verilated.h"

using namespace std;

class SimTime {
    vluint64_t main_time, time_limit, fast_forward;
    vluint64_t main_next;
    int verbose_ticks;
    bool toggle;
    int PERIOD, SEMIPERIOD, CLKSTEP;
public:
    void set_period( int _period ) {
        PERIOD =_period;
        PERIOD += PERIOD%2; // make it even
        SEMIPERIOD = PERIOD>>1;
        // CLKSTEP = SEMIPERIOD>>1;
        CLKSTEP = SEMIPERIOD;
    }
    int period() { return PERIOD; }
    SimTime() { 
        main_time=0; fast_forward=0; time_limit=0; toggle=false;
        verbose_ticks = 48000*24/2;
        set_period(132*6);
    }

    void set_time_limit(vluint64_t t) { time_limit=t; }
    bool limited() { return time_limit!=0; }
    vluint64_t get_time_limit() { return time_limit; }
    vluint64_t get_time() { return main_time; }
    int get_time_s() { return main_time/1000000000; }
    int get_time_ms() { return main_time/1000000; }
    bool next_quarter() {
        if( !toggle ) {
            main_next = main_time + SEMIPERIOD;
            main_time += CLKSTEP;
            toggle = true;
            return false; // toggle clock
        }
        else {
            main_time = main_next;
            if( --verbose_ticks == 0 ) {
                // cerr << "Current time " << dec << (int)(main_time/1000000) << " ms\n";               
                cerr << '.';
                verbose_ticks = 48000*24/2;
            }
            toggle=false;
            return true; // do not toggle clock
        }
    }
    bool finish() { return main_time > time_limit && limited(); }
};

vluint64_t main_time = 0;      // Current simulation time
// This is a 64-bit integer to reduce wrap over issues and
// allow modulus.  You can also use a double, if you wish.

double sc_time_stamp () {      // Called by $time in Verilog
   return main_time;           // converts to double, to match
                               // what SystemC does
}

class CmdWritter {
    int addr, cmd, val;
    Vtop *top;
    bool done;
    int last_clk;
    int state;
    int watch_addr, watch_ch;
    list<FeatureUse>features;
    struct Block_def{ int cmd_mask, cmd, blk_addr; 
        int (*filter)(int);
    };
    list<Block_def>blocks;
    // map<int>YMReg mirror;
public:
    CmdWritter( Vtop* _top );
    void Write( int _addr, int _cmd, int _val );
    void block( int cmd_mask, int cmd, int (*filter)(int), int blk_addr=3 ) {
        Block_def aux;
        aux.cmd_mask = cmd_mask;
        aux.cmd = cmd;
        aux.filter = filter;
        aux.blk_addr = blk_addr;
        cout << "Added block to " << hex << cmd_mask << " - " << cmd << "/ ADDR=" << blk_addr << '\n';
        blocks.push_back( aux );
    };
    void watch( int addr, int ch ) { watch_addr=addr; watch_ch=ch; }
    void Eval();
    bool Done() { return done; }
    void report_usage();
};

class WaveWritter {
    ofstream fsnd, fhex;
    bool dump_hex;
public:
    WaveWritter(const char *filename, int sample_rate, bool hex );
    void write( int16_t *lr );
    ~WaveWritter();
};

class PSGCmdWritter {
    int val;
    Vtop *top;
    bool done;
    int last_clk;
    int state, write_cnt;
public:
    PSGCmdWritter( Vtop* _top );
    void Write( int _val );
    void Eval();
    bool Done() { return done; }
};

struct YMcmd { int addr; int cmd; int val; };

int main(int argc, char** argv, char** env) {
    Verilated::commandArgs(argc, argv);
    Vtop* top = new Vtop;
    CmdWritter writter(top);
    PSGCmdWritter psg_writter(top);
    bool trace = false, slow=false;
    RipParser *gym;
    bool forever=true, dump_hex=false;
    char wav_filename[512]="";
    char *gym_filename;
    SimTime sim_time;
    int SAMPLERATE=0;
    vluint64_t SAMPLING_PERIOD=0;

    for( int k=1; k<argc; k++ ) {
        if( string(argv[k])=="-trace" ) { trace=true; continue; }
        if( string(argv[k])=="-slow" )  { slow=true;  continue; }
        if( string(argv[k])=="-hex" )  { dump_hex=true;  continue; }
        if( string(argv[k])=="-gym" ) { 
            gym_filename = argv[++k];
            gym = ParserFactory( gym_filename, sim_time.period() );
            if( gym==NULL ) return 1;
            continue;
        }
        if( string(argv[k])=="-o" ) { 
            if( ++k == argc ) { cout << "ERROR: expecting filename after -o\n"; return 1; }
            strncpy( wav_filename, argv[k], 512 );
            continue;
        }
        if( string(argv[k])=="-time" ) { 
            int aux;
            sscanf(argv[++k],"%d",&aux);
            vluint64_t time_limit = aux;
            time_limit *= 1000000;
            forever=false;
            cout << "Simulate until " << time_limit/1000000 << "ms\n";
            sim_time.set_time_limit( time_limit );
            continue; 
        }
        if( string(argv[k])=="-noam" ) {
            writter.block( 0xF0, 0x60, [](int v){return v&0x7f;} ); 
            continue;
        }
        if( string(argv[k])=="-noks") {
            writter.block( 0xF0, 0x50, [](int v){return v&0x1f;} ); 
            continue;
        }
        if( string(argv[k])=="-nomul") {
            cout << "All writes to MULT locked to 1\n";
            writter.block( 0xF0, 0x30, [](int v){ return (v&0x70)|1;} ); 
            continue;
        }       
        if( string(argv[k])=="-nodt") {
            cout << "All writes to DT locked to 1\n";
            writter.block( 0xF0, 0x30, [](int v){ return (v&0x0F)|1;} ); 
            continue;
        }
        if( string(argv[k])=="-nossg") {
            cout << "All writes to FM's SSG-EG locked to 0\n";
            writter.block( 0xF0, 0x90, [](int v){ return 0;} ); 
            continue;
        }
        if( string(argv[k])=="-nopsg") {
            cout << "Disabling PSG sound\n";
            writter.block( 0xF0, 0, [](int v){ return 0;} );
            continue;
        }
        if( string(argv[k])=="-mute") {
            int ch;
            if( sscanf(argv[++k],"%d",&ch) != 1 ) {
                cout << "ERROR: needs channel number after -mute\n";
                return 1;
            }
            if( ch<0 || ch>5 ) {
                cout << "ERROR: muted channel must be within 0-5 range\n";
                return 1;
            }
            cout << "Channel " << ch << " muted\n";
            switch(ch) {
                case 0: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==0? 0 : v;} ); break;
                case 1: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==1? 0 : v;} ); break;
                case 2: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==2? 0 : v;} ); break;
                case 3: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==4? 0 : v;} ); break;
                case 4: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==5? 0 : v;} ); break;
                case 5: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==6? 0 : v;} ); break;
            }
            continue;
        }
        if( string(argv[k])=="-only") {
            int ch;
            if( sscanf(argv[++k],"%d",&ch) != 1 ) {
                cout << "ERROR: needs channel number after -only\n";
                return 1;
            }
            if( ch<0 || ch>5 ) {
                cout << "ERROR: channel must be within 0-5 range\n";
                return 1;
            }
            cout << "Only channel " << ch << " will be played\n";
            for( int k=0; k<6; k++ ) {
                if( k==ch ) continue;
                switch(k) {
                    case 0: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==0? 0 : v;} ); break;
                    case 1: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==1? 0 : v;} ); break;
                    case 2: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==2? 0 : v;} ); break;
                    case 3: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==4? 0 : v;} ); break;
                    case 4: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==5? 0 : v;} ); break;
                    case 5: writter.block( 0xFF, 0x28, [](int v)->int{ return (v&0xf)==6? 0 : v;} ); break;
                }
            }
            continue;
        }       
        cout << "ERROR: Unknown argument " << argv[k] << "\n";
        return 1;
    }
    if( strlen(wav_filename)==0 ) {
        strncpy( wav_filename, gym_filename, 512 );
        int dot = strlen( wav_filename) - 3;
        wav_filename[dot]=0;
        strcat( wav_filename, "wav");
    }

    // determines the chip type
    switch( gym->chip() ) {
        case RipParser::ym2203: cout << "YM2203 tune.\n"; break;
        case RipParser::ym2612: cout << "YM2612 tune.\n"; break;
        default: cout << "ERROR: Unknown chip (" << gym->chip() << ") in VGM file\n"; return 1;
    }

    if( gym->period() != 0 ) {
        int clkdiv=6;
        if( gym->chip() == RipParser::ym2203 ) clkdiv=3;
        if( slow ) clkdiv=1;
        int period = gym->period()*clkdiv;
        cout << "Setting PERIOD to " << dec << period << " ns\n";
        sim_time.set_period( period );
    }
    SAMPLING_PERIOD = sim_time.period() * 4 * (gym->chip() == RipParser::ym2203? 3 : 6);
    if( slow ) SAMPLING_PERIOD *= gym->chip() == RipParser::ym2203 ? 3 : 6;
    // if( slow ) SAMPLING_PERIOD *= 6;
    SAMPLERATE = 1.0/(SAMPLING_PERIOD*1e-9);
    cout << "Sample rate " << dec << SAMPLERATE << " Hz. Sampling period " << SAMPLING_PERIOD << "ns\n";

    if( gym->length() != 0 && !sim_time.limited() ) sim_time.set_time_limit( gym->length() );

    VerilatedVcdC* tfp = new VerilatedVcdC;
    if( trace ) {
        Verilated::traceEverOn(true);
        top->trace(tfp,99);
        tfp->open("test.vcd"); 
    }

    // Reset
    top->rst = 1;
    top->clk = 0;
    top->cen = 1;
    top->din = 0;
    top->addr = 0;
    top->cs_n = 0;
    top->wr_n = 1;
    top->psg_wr_n = 1;
    // cout << "Reset\n";
    while( sim_time.get_time() < 256*sim_time.period() ) {
        top->eval();
        if( sim_time.next_quarter() ) top->clk = 1-top->clk;
        // if(trace) tfp->dump(main_time);
    }
    top->rst = 0;
    int last_a=0;
    enum { WRITE_REG, WRITE_VAL, WAIT_FINISH } state;
    state = WRITE_REG;
    
    vluint64_t timeout=0;
    bool wait_nonzero=true;
    const int check_step = 200;
    int next_check=check_step;
    int reg, val;
    bool fail=true;
    // cout << "Main loop\n";
    vluint64_t wait=0;
    int last_sample=0;
    WaveWritter wav(wav_filename, SAMPLERATE, dump_hex);

    // forced values
    list<YMcmd> forced_values;
    // forced_values.push_back( {0, 0xb4, 0x40} );
    // forced_values.push_back( {0, 0xb5, 0x40} );
    // forced_values.push_back( {0, 0xb6, 0x40} );
    // forced_values.push_back( {1, 0xb4, 0x80} ); // canal malo
    // forced_values.push_back( {1, 0xb5, 0x40} );
    // forced_values.push_back( {1, 0xb6, 0x40} ); // no es
    // main loop
    // writter.watch( 1, 0 ); // top bank, channel 0
    bool skip_zeros=true;
    vluint64_t adjust_sum=0;
    int next_verbosity = 200;
    vluint64_t next_sample=0;
    while( forever || !sim_time.finish() ) {
        top->eval();
        if( sim_time.next_quarter() ) {
            int clk = top->clk;
            top->clk = 1-clk;
            // int dout = top->dout;
            if( sim_time.get_time() > next_sample ) {
                int16_t snd[3];
                snd[0] = top->snd_left;
                snd[1] = top->snd_right;
                snd[2] = top->psg_snd*2;
                // skip initial set of zero's
                if( !skip_zeros || snd[0]!=0 || snd[1]!=0 || snd[2]!=0) {
                    skip_zeros=false;
                    wav.write( snd );
                }
                next_sample = sim_time.get_time() + SAMPLING_PERIOD;
            }
            last_sample = top->snd_sample;
            writter.Eval();
            psg_writter.Eval();

            if( timeout!=0 && sim_time.get_time()>timeout ) {               
                cout << "Timeout waiting for BUSY to clear\n";
                cout << "writter.done == " << writter.Done() << '\n';
                goto finish;
            }
            if( sim_time.get_time() < wait ) continue;
            if( !writter.Done() || !psg_writter.Done() ) continue;

            if( !forced_values.empty() ) {
                const YMcmd &c = forced_values.front();
                cout << "Forced value\n";
                writter.Write( c.addr, c.cmd, c.val );
                forced_values.pop_front();
                continue;
            }

            int action;
            action = gym->parse();
            switch( action ) {
                default: 
                    if( !sim_time.finish() ) {
                        cout << "go on\n";
                        continue;
                    }
                    goto finish;
                case RipParser::cmd_psg:
                    // cout << "PSG Write @" << sim_time.get_time() <<"\n";
                    psg_writter.Write(gym->cmd);
                    timeout = 0;
                    break;
                case RipParser::cmd_write: 
                    // if( /*(gym->cmd&(char)0xfc)==(char)0xb4 ||*/
                    // /*(gym->addr==0 && gym->cmd>=(char)0x30) || */
                    // ((gym->cmd&(char)0xf0)==(char)0x90)) {
                    //   cout << "Skipping write to " << hex << (gym->cmd&0xff) << " register\n" ;
                    //  break; // do not write to RL register
                    // }
                    // cout << "CMD = " << hex << ((int)gym->cmd&0xff) << '\n';
                    writter.Write( gym->addr, gym->cmd, gym->val );
                    timeout = sim_time.get_time() + sim_time.period()*6*100;
                    break; // parse register
                case RipParser::cmd_wait: 
                    // cout << "Waiting\n";
                    wait=gym->wait;
                    // cout << "Wait for " << dec << wait << "ns (" << wait/1000000 << " ms)\n";
                    // if(trace) wait/=3;
                    wait+=sim_time.get_time();
                    timeout=0;
                    break;// wait 16.7ms    
                case RipParser::cmd_finish: // reached end of file
                    goto finish;
                case RipParser::cmd_error: // unsupported command
                    goto finish;                
            }
        }
        if(trace) tfp->dump(sim_time.get_time());
    }
finish:
    writter.report_usage();
    if( skip_zeros ) {
        cout << "WARNING: Output wavefile is empty. No sound output was produced.\n";
    }

    if( main_time>1000000000 ) { // sim lasted for seconds
        cout << "$finish at " << dec << sim_time.get_time_s() << "s = " << sim_time.get_time_ms() << " ms\n";
    } else {
        cout << "$finish at " << dec << sim_time.get_time_ms() << "ms = " << sim_time.get_time() << " ns\n";
    }
    if(trace) tfp->close(); 
    delete gym;
    delete top;
 }


void WaveWritter::write( int16_t* lr ) {
    int16_t g[2];
    g[0] = lr[0]+lr[2]; // Left  + PSG
    g[1] = lr[1]+lr[2]; // right + PSG
    fsnd.write( (char*)&g, sizeof(int16_t)*2 );
    if( dump_hex ) {
        fhex << hex << g[0] << '\n';
        fhex << hex << g[1] << '\n';
    }
}

WaveWritter::WaveWritter( const char *filename, int sample_rate, bool hex ) {
    fsnd.open(filename, ios_base::binary);
    dump_hex = hex;
    if( dump_hex ) {
        char *hexname;
        hexname = new char[strlen(filename)+1];
        strcpy(hexname,filename);
        strcpy( hexname+strlen(filename)-4, ".hex" );
        cout << "Hex file " << hexname << '\n';
        fhex.open(hexname);
        delete[] hexname;
    }
    // write header
    char zero=0;
    for( int k=0; k<45; k++ ) fsnd.write( &zero, 1 );
    fsnd.seekp(0);
    fsnd.write( "RIFF", 4 );
    fsnd.seekp(8);
    fsnd.write( "WAVEfmt ", 8 );
    int32_t number32 = 16;
    fsnd.write( (char*)&number32, 4 );
    int16_t number16 = 1;
    fsnd.write( (char*) &number16, 2);
    number16=2;
    fsnd.write( (char*) &number16, 2);
    number32 = sample_rate; 
    fsnd.write( (char*)&number32, 4 );
    number32 = sample_rate*2*2; 
    fsnd.write( (char*)&number32, 4 );
    number16=2*2;   // Block align
    fsnd.write( (char*) &number16, 2);
    number16=16;
    fsnd.write( (char*) &number16, 2);
    fsnd.write( "data", 4 );
    fsnd.seekp(44); 
}

WaveWritter::~WaveWritter() {
    int32_t number32;
    streampos file_length = fsnd.tellp();
    number32 = (int32_t)file_length-8;
    fsnd.seekp(4);
    fsnd.write( (char*)&number32, 4);
    fsnd.seekp(40);
    number32 = (int32_t)file_length-44;
    fsnd.write( (char*)&number32, 4);   
}


void CmdWritter::report_usage() {
    cout << "Features used: \t";
    for( const auto& k : features )
        if(k.is_used()) cout << k.name() << ' ';
    cout << '\n';
}

CmdWritter::CmdWritter( Vtop* _top ) {
    top  = _top;
    last_clk = 0;
    done = true;
    features.push_back( FeatureUse("DT",   0xF0, 0x30, 0x70, [](char v)->bool{return v!=0;} ));
    features.push_back( FeatureUse("MULT", 0xF0, 0x30, 0x0F, [](char v)->bool{return v!=1;} ));
    features.push_back( FeatureUse("KS",   0xF0, 0x50, 0xC0, [](char v)->bool{return v!=0;} ));
    features.push_back( FeatureUse("AM",   0xF0, 0x60, 0x80, [](char v)->bool{return v!=0;} ));
    features.push_back( FeatureUse("SSG",  0xF0, 0x90, 0x08, [](char v)->bool{return v!=0;} ));
    watch_ch = -1;
    //add_op_mirror( 0x30, "DT", 0x70, 2, )
}

void CmdWritter::Write( int _addr, int _cmd, int _val ) {
    // cout << "Writter command\n";
    for( auto&k : blocks ) {
        int aux = _cmd;
        aux &= k.cmd_mask;
        if( aux == k.cmd && (k.blk_addr==3 || k.blk_addr==_addr )) {
            int old=_val;
            _val = k.filter(_val);
        }
    }
    addr = _addr;
    cmd  = _cmd;
    val  = _val;
    done = false;
    state = 0;
    if( addr == watch_addr && cmd>=(char)0x30 && (cmd&0x3)==watch_ch )
        cout << addr << '-' << watch_ch << " CMD = " << hex << (cmd&0xff) << " VAL = " << (val&0xff) << '\n';
    for( auto& k : features )
        k.check( cmd, val );    
    // cout << addr << '\t' << hex << "0x" << ((unsigned)cmd&0xff);
    // cout  << '\t' << ((unsigned)val&0xff) << '\n' << dec;
}

void CmdWritter::Eval() {   
    // cout << "Writter eval " << state << "\n";
    int clk = top->clk; 
    if( (clk==0) && (last_clk != clk) ) {
        switch( state ) {
            case 0: 
                top->addr = addr ? 2 : 0;
                top->din = cmd;
                top->wr_n = 0;
                state=1;
                break;
            case 1:
                top->wr_n = 1;
                state = 2;
                break;
            case 2:
                top->addr = ((int)top->addr) + 1;
                top->din = val;
                top->wr_n = 0;
                state = 3;
                break;
            case 3:
                top->wr_n = 1;
                state=4;
                break;
            case 4:             
                if( (((int)top->dout) &0x80 ) == 0 ) {
                    done = true;
                    state=5;
                }
                break;
            default: break;
        }
    }
    last_clk = clk;
}

PSGCmdWritter::PSGCmdWritter( Vtop* _top ) {
    top  = _top;
    last_clk = 0;
    done = true;
}

void PSGCmdWritter::Write( int _val ) {
    val  = _val;
    done = false;
    state = 0;
}

void PSGCmdWritter::Eval() {   
    int clk = top->clk; 
    if( (clk==0) && (last_clk != clk) ) {
        switch( state ) {
            case 0: 
                top->din = val;
                top->psg_wr_n = 0;
                state=1;
                write_cnt=3;
                break;
            case 1:
                if(--write_cnt<=0) {
                    top->psg_wr_n = 1;
                    state = 2;
                }
                break;
            case 2:             
                done = true;
                break;
            default: break;
        }
    }
    last_clk = clk;
}