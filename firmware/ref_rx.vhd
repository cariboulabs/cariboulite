library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.STD_LOGIC_UNSIGNED.ALL;
use IEEE.NUMERIC_STD.ALL;

library UNISIM;
use UNISIM.VComponents.all;

entity at86rf215_rx_interface is
    Port (
          reset_n   : in STD_LOGIC;
          rxd_09_24 : in STD_LOGIC_VECTOR(1 downto 0);
          rxclk     : in STD_LOGIC;  --64 MHz clk from Radio Chip

          IQenable  : in  std_logic;
          Isample_o : out std_logic_vector(15 downto 0);
          Qsample_o : out std_logic_vector(15 downto 0);
          IQValid_o : out std_logic;

          rx_bits_o : out std_logic_vector(1 downto 0);
          rxclk_o   : out std_logic
        );
end at86rf215_rx_interface;

architecture Behavioral of at86rf215_rx_interface is

-- differential signals
signal rx_bits   : std_logic_vector(1 downto 0) := "00";
signal rx_bits_d : std_logic_vector(1 downto 0) := "00";

--output data
signal I_Sample  : std_logic_vector(13 downto 0) := (others=>'0');
signal Q_Sample  : std_logic_vector(13 downto 0) := (others=>'0');

-- state variables
signal ibit_counter  : std_logic_vector(3 downto 0) := x"0";
signal inc_icntr     : std_logic := '0';
signal icntr_ovf     : std_logic := '0';
signal qbit_counter  : std_logic_vector(3 downto 0) := x"0";
signal inc_qcntr     : std_logic := '0';
signal qcntr_ovf     : std_logic := '0';
signal ien,qen       : std_logic := '0';

constant I_SYNC      : std_logic_vector := "10";
constant Q_SYNC      : std_logic_vector := "01";

type state_type is (IDLE,ISAMPLE,QSAMPLE);
signal current_state, next_state : state_type := IDLE;

begin


-- debug IO's
    rxclk_o   <= rxclk;
    rx_bits_o <= rx_bits_d;

-- RX FSM
    Rx_state_transit : process(reset_n,rxclk)
    begin
        if(reset_n ='0') then
            rx_bits_d     <= "00";
            current_state <= IDLE;
        else
            if(rising_edge(rxclk)) then
                --rx_bits are swapped, as selectio output bits are somehow shifted
                rx_bits_d     <= rxd_09_24(0) & rxd_09_24(1);
                current_state <= next_state;
            end if;
        end if;
    end process;

    Rx_Sampling_FSM : process(current_state,rx_bits_d,icntr_ovf,qcntr_ovf)
    begin
        inc_icntr <= '0';
        inc_qcntr <= '0';
        ien <= '0';
        qen <= '0';
        case current_State is
            when IDLE =>
                if (IQenable  = '1') then
                    if(rx_bits_d = I_SYNC) then
                        next_state <= ISAMPLE;
                    else
                        next_state <= IDLE;
                    end if;
                end if;
            when ISAMPLE =>
                if(icntr_ovf = '1') then
                    if(rx_bits_d = Q_SYNC) then
                        next_state <= QSAMPLE;
                    else
                        next_state <= IDLE;
                    end if;
                else
                    inc_icntr <= '1';
                    ien <= '1';
                    next_state <= ISAMPLE;
                end if;

            when QSAMPLE =>
                if(qcntr_ovf = '1') then
                    if(rx_bits_d = I_SYNC) then
                        next_state <= ISAMPLE;
                    else
                        next_state <= IDLE;
                    end if;
                else
                    inc_qcntr <= '1';
                    qen <= '1';
                    next_state <= QSAMPLE;
                end if;

            when others =>
                    next_state <= IDLE;
        end case;
    end process;

    cntr_proc: process(reset_n,rxclk)
    begin
        if(reset_n = '0') then
            ibit_counter <= x"0";
            icntr_ovf <= '0';
            qbit_counter <= x"0";
            qcntr_ovf <= '0';
        else
            if(rising_edge(rxclk)) then
                if(ien = '1') then
                    I_Sample <= I_Sample(11 downto 0) & rx_bits_d;
                end if;
                if(qen = '1') then
                    Q_Sample <= Q_Sample(11 downto 0) & rx_bits_d;
                end if;
                if(inc_icntr = '1') then
                    if(ibit_counter < 6) then
                        ibit_counter <= ibit_counter + 1;
                        icntr_ovf <= '0';
                    else
                        ibit_counter <= X"0";
                        icntr_ovf <= '1';
                    end if;
                else
                    icntr_ovf <= '0';
                end if;
                if(inc_qcntr = '1') then
                    if(qbit_counter < 6) then
                        qbit_counter <= qbit_counter + 1;
                        qcntr_ovf <= '0';
                    else
                        qbit_counter <= X"0";
                        qcntr_ovf <= '1';
                    end if;
                else
                    qcntr_ovf <= '0';
                end if;
            end if;
        end if;
    end process;

    IQ_sample_output_process: process(reset_n,rxclk)
    begin
         if(reset_n = '0') then
            ISample_o <= (others => '0');
            QSample_o <= (others => '0');
            IQValid_o <= '0';
         else
             if(rising_edge(rxclk)) then
                 if(icntr_ovf = '1') then
                     ISample_o <=  I_Sample(13) & I_Sample(13) & I_Sample(13 downto 0);
                end if;

                if(qcntr_ovf = '1') then
                     QSample_o <= Q_Sample(13) &  Q_Sample(13) & Q_Sample(13 downto 0);
                     IQValid_o <= '1';
                else
                     IQValid_o <= '0';

                end if;
             end if;
         end if;
     end process;
end Behavioral;
