/*  This file is part of JT12.

	JT12 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	JT12 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with JT12.  If not, see <http://www.gnu.org/licenses/>.

	Author: Jose Tejada Gomez. Twitter: @topapate
	Version: 1.0
	Date: 29-10-2018

	*/

module jt12_eg_ctrl(
	input			 	rst,
	input				keyon_now,
	input				keyoff_now,
	input		[2:0]	state_in,
	// envelope configuration	
	input		[4:0]	arate, // attack  rate
	input		[4:0]	rate1, // decay   rate
	input		[4:0]	rate2, // sustain rate
	input		[3:0]	rrate,
	input		[3:0]	sl,   // sustain level
	// SSG operation
	input				ssg_en,
	input				ssg_eg,
	// SSG output inversion
	input				ssg_inv_in,
	output reg			ssg_inv_out,
	// SSG output hold
	input				ssg_lock_in,
	output reg			ssg_lock_out,

	output reg	[3:0]	base_rate,
	output reg			attack,
	output reg	[2:0]	state_next,
	output reg			pg_rst
);

localparam 	ATTACK = 3'b001, 
			DECAY  = 3'b010, 
			HOLD   = 3'b100,
			RELEASE= 3'b000; // default state is release 

wire is_decaying = state_in[1] | state_in[2];

reg		[4:0]	sustain;

always @(*) if( clk_en ) begin
	if( sl == 4'd15 )
		sustain = 5'h1f; // 93dB
	else
		sustain = {1'b0, sl};
end

wire	ssg_en_out;
wire	keyon_last;
reg		ssg_en_in, ssg_pg_rst;

// aliases
wire ssg_att  = ssg_eg[2];
wire ssg_alt  = ssg_eg[1];
wire ssg_hold = ssg_eg[0] & ssg_en;
reg ssg_over;


always @(*) begin
	ssg_over = ssg_en && (eg[9:8] >= 2'd0); // eg >=10'h200
	ssg_pg_rst = ssg_over && !( ssg_alt || ssg_hold );
	pg_rst = keyon_now | ssg_pg_rst;
end

always @(*) begin
	// ar_off	= arate == 5'h1f;
	// trigger release
	if( keyoff_now ) begin
		base_rate = { rrate, 1'b1 };
		state_next = RELEASE;
		ssg_inv_out = ssg_inv_in & ssg_en;
		ssg_lock_out= 1'b0;
	end
	else begin
		// trigger 1st decay
		if( keyon_now ) begin
			base_rate	= arate;
			state_next	= ATTACK;
			ssg_inv_out	= ssg_att & ssg_en;
			ssg_lock_out= 1'b0;
		end
		else begin : sel_rate
			case ( state_in )
				ATTACK: 
					if( eg==10'd0 ) begin
						base_rate	= ssg_lock_in ? 5'd0 : rate1;
						state_next	= ssg_lock_in ? HOLD : DECAY;
						ssg_inv_out	= ssg_en & (ssg_alt ^ ssg_inv_in);
						ssg_lock_out= ssg_hold;
					end
					else begin
						base_rate	= arate;
						state_next	= ATTACK;
						ssg_inv_out	= ssg_inv_in;
						ssg_lock_out= ssg_lock_in;
					end
				DECAY: begin
					ssg_inv_out = ssg_inv_in;
					if( ssg_over ) begin
						base_rate	= arate;
						state_next	= ATTACK;
						ssg_lock_out= ssg_hold;
					end
					else begin
						base_rate	=  eg[9:5] >= sustain ? rate2 : rate1;
						state_next	= DECAY;
						ssg_lock_out= 1'b0;
					end
				end
				HOLD: begin
					base_rate	= 5'd0;
					state_next	= HOLD;
					ssg_inv_out	= ssg_inv_in;
					ssg_lock_out= 1'b1;
				end
				default: begin // RELEASE
					base_rate	= { rrate, 1'b1 };
					state_next	= RELEASE;	// release
					ssg_lock_out= 1'b0;
					ssg_inv_out	= 1'b0; // this can produce a glitch in the output
						// But to release from SSG cannot be done nicely while
						// inverting the ouput
				end
			endcase
		end
	end
end


endmodule // jt12_eg_ctrl