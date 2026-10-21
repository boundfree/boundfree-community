#include "ica_core.h"
namespace EEGICA = bf::separation::ica;
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

static int checks=0;
static void require(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> static void rejects(F f,bf::separation::Failure expected) {
    try { f(); } catch (const bf::separation::Error& e) { require(e.reason==expected,"expected failure reason"); return; }
    require(false,"invalid input was accepted");
}
int main()
{
    try {
        constexpr int n=10000;
        constexpr double fs=500,pi=3.14159265358979323846;
        bf::separation::Matrix sources(3,std::vector<double>(n));
        std::mt19937 rng(1949);
        for (int t=0;t<n;++t) {
            const double s=t/fs;
            sources[0][t]=12*std::sin(2*pi*10*s)+4*std::sin(2*pi*63*s);
            sources[1][t]=18*std::sin(2*pi*0.18*s);
            for (double blink:{2.2,5.1,9.3,12.6,17.2}) sources[1][t]+=100*std::exp(-std::pow((s-blink)/0.12,2));
            const double noise=double(rng())/4294967295.0-0.5;
            sources[2][t]=noise*(8+65*std::exp(-std::pow((s-7)/0.5,2))+75*std::exp(-std::pow((s-15)/0.7,2)));
        }
        const double mixing[4][3]={{1,0.5,0.3},{0.3,1.2,0.1},{1,-0.2,1.1},{0.7,0.8,-0.6}};
        bf::separation::Matrix input(4,std::vector<double>(n));
        for (int c=0;c<4;++c) for (int t=0;t<n;++t) { input[c][t]=13+c; for (int k=0;k<3;++k) input[c][t]+=mixing[c][k]*sources[k][t]; }
        EEGICA::Options options;
        auto result=EEGICA::fit(input,options);
        require(result.rank==3,"rank deficient mixture must retain exactly three sources");
        std::array<int,3> permutation{0,1,2},best=permutation;
        double bestScore=0;
        do {
            double score=0;
            for (int k=0;k<3;++k) score+=std::abs(EEGICA::correlation(sources[k],result.sources[permutation[k]]));
            if (score>bestScore) { bestScore=score;best=permutation; }
        } while (std::next_permutation(permutation.begin(),permutation.end()));
        for (int k=0;k<3;++k) {
            const double r=std::abs(EEGICA::correlation(sources[k],result.sources[best[k]]));
            std::cout<<"Synthetic source "<<k<<" |r|="<<r<<'\n';
            require(r>0.94,"controlled source recovery below threshold");
        }
        std::vector<int> groups(3,3);
        for (int k=0;k<3;++k) groups[best[k]]=k;
        std::vector<bf::separation::Matrix> projected;
        for (int g=0;g<4;++g) projected.push_back(EEGICA::project(result,groups,g));
        double error=0,brainError=0,brainPower=0;
        for (int c=0;c<4;++c) for (int t=0;t<n;++t) {
            double sum=result.mean[c]+result.residual[c][t];
            for (int g=0;g<4;++g) sum+=projected[g][c][t];
            error=std::max(error,std::abs(sum-input[c][t]));
            const double truth=mixing[c][0]*sources[0][t];
            brainError+=std::pow(projected[0][c][t]-truth,2);brainPower+=truth*truth;
        }
        std::cout<<"Reconstruction max error uV="<<error<<"; EEG projection RRMSE="<<std::sqrt(brainError/brainPower)<<'\n';
        require(error<1e-9,"partition must reconstruct input including mean and residual");
        require(std::sqrt(brainError/brainPower)<0.15,"brain projection including 63 Hz must survive");
        std::vector<int> unresolved(3,3);
        auto blank=EEGICA::project(result,unresolved,0);
        require(blank[0][300]==0,"unresolved components must not silently become EEG");
        auto repeat=EEGICA::fit(input,options);
        require(result.sources==repeat.sources,"fixed seed must reproduce source order and values");
        rejects([&]{ EEGICA::fit(input,options,[]{return true;}); },bf::separation::Failure::Cancelled);
        auto invalid=input;invalid[0][2]=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{ EEGICA::fit(invalid,options); },bf::separation::Failure::NonFinite);
        invalid=input; std::fill(invalid[0].begin(),invalid[0].end(),5);
        rejects([&]{ EEGICA::fit(invalid,options); },bf::separation::Failure::FlatChannel);
        invalid={input[0],input[0]};
        rejects([&]{ EEGICA::fit(invalid,options); },bf::separation::Failure::LowRank);
        rejects([&]{ EEGICA::fit({input[0]},options); },bf::separation::Failure::InvalidInput);
        options.maxIterations=1;
        rejects([&]{ EEGICA::fit(input,options); },bf::separation::Failure::NotConverged);
        // Full-rank 16-observation fixture exercises whitening/decorrelation at the target scale.
        bf::separation::Matrix sixteen(16,std::vector<double>(5000)), independent=sixteen;
        for (int c=0;c<16;++c) for (int t=0;t<5000;++t) {
            const double u=(double(rng())+0.5)/4294967296.0-0.5;
            independent[c][t]=-std::copysign(1.0,u)*std::log(1-2*std::abs(u));
        }
        for (int c=0;c<16;++c) for (int k=0;k<16;++k) for (int t=0;t<5000;++t)
            sixteen[c][t]+=(c==k?5.0:0.2*std::sin(c+2*k+1))*independent[k][t];
        options.maxIterations=600;
        auto full=EEGICA::fit(sixteen,options);
        require(full.rank==16,"16-observation rank retained");
        double worst=1;
        for (int c=0;c<16;++c) {
            double bestCorrelation=0;
            for (int k=0;k<16;++k) bestCorrelation=std::max(bestCorrelation,std::abs(EEGICA::correlation(independent[c],full.sources[k])));
            worst=std::min(worst,bestCorrelation);
        }
        std::cout<<"16-channel synthetic worst |r|="<<worst<<"; iterations="<<full.iterations<<'\n';
        require(worst>0.95,"16-channel controlled source recovery");
        std::cout<<"PASS "<<checks<<" ICA core checks (synthetic data only)\n";
    } catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
