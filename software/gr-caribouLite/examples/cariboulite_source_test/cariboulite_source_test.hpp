#ifndef CARIBOULITE_SOURCE_TEST_HPP
#define CARIBOULITE_SOURCE_TEST_HPP
/********************
GNU Radio C++ Flow Graph Header File

Title: Cariboulite Source Test
Author: Alon and David
GNU Radio version: v3.11.0.0git-604-gd7f88722
********************/

/********************
** Create includes
********************/
#include <gnuradio/top_block.h>
#include <gnuradio/caribouLite/caribouLiteSource.h>
#include <gnuradio/blocks/null_sink.h>



using namespace gr;



class cariboulite_source_test {

private:


    caribouLite::caribouLiteSource::sptr caribouLite_caribouLiteSource_0;
    blocks::null_sink::sptr blocks_null_sink_0;



public:
    top_block_sptr tb;
    cariboulite_source_test();
    ~cariboulite_source_test();


};


#endif

