//  Author: Arjun Ramaswami

#include <stdio.h>
#include <stdlib.h> // EXIT_FAILURE
#include <math.h>
#include <stdbool.h>

#include "CL/opencl.h"
#include "fftfpga/fftfpga.h"

#include "argparse.h"
#include "helper.h"
#include "verify_fftw.h"

static const char *const usage[] = {
    "bin/host [options]",
    NULL,
};

int main(int argc, const char **argv) {
  int N = 64, dim = 3, iter = 1, inv = 0, sp = 0, use_bram = 0;
  char *path = "fft3d_emulate.aocx";
  const char *platform = "Intel(R) FPGA";
  fpga_t timing = {0.0, 0.0, 0.0, 0};
  int use_svm = 0, use_emulator = 0;
  double avg_rd = 0.0, avg_wr = 0.0, avg_exec = 0.0;
  double temp_timer = 0.0, total_api_time = 0.0;
  bool status = true;

  struct argparse_option options[] = {
    OPT_HELP(),
    OPT_GROUP("Basic Options"),
    OPT_INTEGER('n',"n", &N, "FFT Points"),
    OPT_BOOLEAN('s',"sp", &sp, "Single Precision"),
    OPT_INTEGER('i',"iter", &iter, "Iterations"),
    OPT_BOOLEAN('b',"back", &inv, "Backward FFT"),
    OPT_BOOLEAN('v',"svm", &use_svm, "Use SVM"),
    OPT_BOOLEAN('m',"bram", &use_bram, "Use BRAM"),
    OPT_STRING('p', "path", &path, "Path to bitstream"),
    OPT_END(),
  };

  struct argparse argparse;
  argparse_init(&argparse, options, usage, 0);
  argparse_describe(&argparse, "Computing FFT using FPGA", "FFT size and dimensions are mandatory, default dimension and number of iterations are 1");
  argc = argparse_parse(&argparse, argc, argv);

  // Print to console the configuration chosen to execute during runtime
  print_config(N, dim, iter, inv, sp, use_bram);
  
  int isInit = fpga_initialize(platform, path, use_svm, use_emulator);
  if(isInit != 0){
    return EXIT_FAILURE;
  }

  if(sp == 0){
    printf("Not implemented. Work in Progress\n");
    return EXIT_SUCCESS;
  } 
  else{
    for(size_t i = 0; i < iter; i++){

      // create and destroy data every iteration
      size_t inp_sz = sizeof(float2) * N * N * N;
      float2 *inp = (float2*)fftfpgaf_complex_malloc(inp_sz, use_svm);
      float2 *out = (float2*)fftfpgaf_complex_malloc(inp_sz, use_svm);

      status = fftf_create_data(inp, N * N * N);
      if(!status){
        free(inp);
        free(out);
        return EXIT_FAILURE;
      }

      if(use_bram == 1){
        // use bram for 3d Transpose
        temp_timer = getTimeinMilliseconds();
        timing = fftfpgaf_c2c_3d_bram(N, inp, out, inv);
        total_api_time += getTimeinMilliseconds() - temp_timer;
      }
      else{
        // use ddr for 3d Transpose
        temp_timer = getTimeinMilliseconds();
        timing = fftfpgaf_c2c_3d_ddr(N, inp, out, inv);
        total_api_time += getTimeinMilliseconds() - temp_timer;
      }

#ifdef USE_FFTW
      if(!verify_sp_fft3d_fftw(out, inp, N, inv)){
        fprintf(stderr, "3d FFT Verification Failed \n");
        free(inp);
        free(out);
        return EXIT_FAILURE;
      }
#endif
      if(timing.valid == 0){
        fprintf(stderr, "Invalid execution, timing found to be 0");
        free(inp);
        free(out);
        return EXIT_FAILURE;
      }

      avg_rd += timing.pcie_read_t;
      avg_wr += timing.pcie_write_t;
      avg_exec += timing.exec_t;

      // destroy FFT input and output
      free(inp);
      free(out);
    }  // iter
  } // sp condition

  // destroy fpga state
  fpga_final();

  // display performance measures
  display_measures(total_api_time, avg_rd, avg_wr, avg_exec, N, dim, iter, inv, sp);

  return EXIT_SUCCESS;
}
