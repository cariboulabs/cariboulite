/********************
GNU Radio C++ Flow Graph Source File

Title: Cariboulite Source Test
Author: Alon and David
GNU Radio version: v3.11.0.0git-604-gd7f88722
********************/

#include "cariboulite_source_test.hpp"

using namespace gr;


cariboulite_source_test::cariboulite_source_test ()  {


    this->tb = gr::make_top_block("Cariboulite Source Test");

// Blocks:
        this->caribouLite_caribouLiteSource_0 = caribouLite::caribouLiteSource::make(0, false, 40.0, 2500000.0, 4000000.0, 900000000.0);

        this->blocks_null_sink_0 = blocks::null_sink::make(sizeof(gr_complex)*1);


// Connections:
    this->tb->hier_block2::connect(this->caribouLite_caribouLiteSource_0, 0, this->blocks_null_sink_0, 0);
}

cariboulite_source_test::~cariboulite_source_test () {
}

// Callbacks:

int main (int argc, char **argv) {

    cariboulite_source_test* top_block = new cariboulite_source_test();
    top_block->tb->start();
    std::cout << "Press Enter to quit: ";
    std::cin.ignore();
    top_block->tb->stop();
    top_block->tb->wait();

    return 0;
}
