`timescale 1ns / 1ps

module jt12_test;

reg	rst;
reg	clk;

wire s1_enters, s2_enters, s3_enters, s4_enters;

`include "../common/dump.vh"

initial begin
	clk = 0;
    forever #10 clk=~clk;
end

reg	test_eg;
// envelope configuration
wire	[4:0]	keycode_III = 5'd10;
reg	[4:0]	arate; // attack  rate
reg	[4:0]	rate1; // decay   rate
reg	[4:0]	rate2; // sustain rate
wire	[3:0]	rrate = 4'hf; // release rate
wire	[3:0]	d1l = 4'd8;   // sustain level
wire	[1:0]	ks_III = 2'd0;	   // key scale
// SSG operation
reg	[3:0]	ssg_eg_II;
// envelope operation
reg			keyon;
reg			keyoff;
// envelope number
wire	[6:0]	am = 7'd0;
reg		[6:0]	tl_VII=7'd0;
wire	[1:0]	ams_VII = 2'd0;
wire			amsen_VII = 1'b0;

initial begin
	rst = 0;
	rate1 = 5'd29;
	rate2 = 5'd27;	
    #5 rst = 1;
    #20 rst = 0;
    #(1*1000*1000) $finish;
end

integer cycles;
reg [4:0] cnt24;
wire	zero = cnt24==5'd0;
reg keyon_done;

always @(posedge clk)
	if( rst ) begin
		keyon <= 1'b1;
		keyoff <= 1'b0;
		cycles <= 0;
		keyon_done <= 1'b0;
		ssg_eg_II <= 4'b0;
	end else begin
		cycles <= cycles + 1;
		if( cycles==100 ) keyon<=1'b0;
		if( cycles==110 ) keyoff<=1'b1;		
		if( cycles==134 ) keyoff<=1'b0;				
		
		if( cycles > 10000 ) begin
			keyon <= cycles==10559;
			ssg_eg_II <= 4'b1110;
		end
	end
		

always @(posedge clk)
	if( rst ) begin
		cnt24 <= 0;
	end else begin
		if( cnt24 == 5'd23 )
			cnt24 <= 5'd0;
		else
			cnt24 <= cnt24 + 1;
	end

always @(*)
	if( cycles < 104 )
		arate <= 5'h1f;
	else
		arate <= cnt24==5'd0 ? 5'd31 : 5'd0;

wire	[9:0]	eg;
wire	phase_cnt_rst;

jt12_envelope uut(
	.test_eg(test_eg),
	.rst	(rst),
	.clk	(clk),
	.clk_en	(1'b1),
	.zero	(zero),
// envelope configuration
	.keycode_III(keycode_III),
	.arate(arate), // attack  rate
	.rate1(rate1), // decay   rate
	.rate2(rate2), // sustain rate
	.rrate(rrate), // release rate
	.d1l(d1l),   // sustain level
	.ks_III(ks_III),	   // key scale
// SSG operation
	.ssg_en_I ( cnt24==5'd0 ? ssg_eg_II[3]   : 1'h0 ),
	.ssg_eg_II( cnt24==5'd1 ? ssg_eg_II[2:0] : 3'h0 ),
// envelope operation
	.keyon	(keyon),
	.keyoff	(keyoff),
// envelope number
	.am		(am),
	.tl_VII	(tl_VII),
	.ams_VII(ams_VII),
	.amsen_VII(amsen_VII),

	.eg(eg),
	.phase_cnt_rst(phase_cnt_rst)
);

wire [9:0] eg0;
wire [9:0] rest_and, rest_or;

jt12_opsync u_opsync(
	.rst	( rst		),
	.clk	( clk		),
    .clk6   ( clk6      ),
	.s1_enters	( s1_enters ),
	.s2_enters	( s2_enters ),
	.s3_enters	( s3_enters ),
	.s4_enters	( s4_enters )
);    

sep24 #(.width(10),.pos0(8)) sep(
	.clk	( clk	),
	.mixed	( eg	),
	.cnt	( cnt24	),
	.ch0op0	( eg0	),
	.alland	( rest_and ),
	.allor	( rest_or  ),
	.mask	( ~24'b1 )
);

endmodule
