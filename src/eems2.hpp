#pragma once

#include "util.hpp"
#include "mcmc.hpp"
#include "draw.hpp"
#include "graph.hpp"
#include "habitat.hpp"

#ifndef EEMS2_H
#define EEMS2_H

/*
 An updated set of parameter values
 The type of move is necessary in order to know which parameters have a new proposed value;
 the rest of the parameters won't be set to their current values (to avoid unnecessary copying)
 For example, if move = M_VORONOI_BIRTH_DEATH,
 then newmtiles, newmSeeds, nowmColors, newmEffcts (and of course, newpi, newll, newratioln) would have changed
 if move = M_VORONOI_POINT_MOVE,
 then newmSeeds, nowmColors (and of course, newpi, newll) would have changed
 The ratioln is the proposal ratio for birth/death proposal.
 For the usual Metropolis-Hastings updates, the acceptance probability is
 alpha = (prior ratio) * (likelihood ratio)
 For the birth/deatch RJ-MCMC updates, the acceptance probability is
 alpha = (proposal ratio) * (prior ratio) * (likelihood ratio)
 See Green, "Reversible jump Markov chain Monte Carlo computation and Bayesian model determination"
 */
struct Proposal {
    MoveType move; // the type of proposal/update
    int newqtiles; // number of m and q tiles, respectively
    int newmtiles;
    double newdf; // degrees of freedom
    double newpi; // log prior
    double newll; // log likelihood
    //double newsigma2; // variance scale
    double newratioln; // RJ-MCMC proposal ratio, on the log scale
    double newmrateMu; // overall (mean) migration rate,
    double newqrateMu;
    
    VectorXd newqEffcts; // the diversity rate of each q tile
    VectorXd newmEffcts; // the migration rate of each m tile, relative to the ovarall mrateMu
    MatrixXd newqSeeds;  // the location of each q tile within the habitat
    MatrixXd newmSeeds;  // the location of each m tile within the habitat
    
};

class EEMS2 {
public:
    
    EEMS2(const Params &params);
    ~EEMS2( );
    
    void initialize_state( );
    void load_final_state( );
    bool start_eems(const MCMC &mcmc);
    double eval_prior(const MatrixXd &mSeeds, const VectorXd &mEffcts, const double mrateMu, const double mrateS2,
                      const MatrixXd &qSeeds, const VectorXd &qEffcts, const double qrateMu, const double qrateS2,
                      const double df) const;
    double eems2_likelihood(MatrixXd newmSeeds, MatrixXd newqSeeds, VectorXd newmEffcts,
                            VectorXd newqEffcts, double newmrateMu, double newdf) const;
    void calculateProduct(VectorXd &z, VectorXd &q, const MatrixXd &M, const VectorXd &W) const;
    void krylovProj(MatrixXd &H, MatrixXd &Q, const MatrixXd &M, const VectorXd &W) const;
    void calculateIntegral(const MatrixXd &M, const MatrixXd &W, MatrixXd &Lambda, double L, double r) const;
    
    MoveType choose_move_type( );
    // These functions change the within demes component:
    double eval_proposal_rate_one_qtile(Proposal &proposal) const;
    double eval_proposal_move_one_qtile(Proposal &proposal) const;
    double eval_birthdeath_qVoronoi(Proposal &proposal) const;
    // These functions change the between demes component:
    double eval_proposal_rate_one_mtile(Proposal &proposal) const;
    double eval_proposal_overall_mrate(Proposal &proposal) const;
    double eval_proposal_overall_qrate(Proposal &proposal) const;
    double eval_proposal_move_one_mtile(Proposal &proposal) const;
    double eval_birthdeath_mVoronoi(Proposal &proposal) const;
    
    // Gibbs updates:
    // Too complex and maybe unnecessary. For now -- keep sigma2 fixed and equal to 1.0
    //void update_sigma2( );
    void update_hyperparams( );
    // Random-walk Metropolis-Hastings proposals:
    void propose_df(Proposal &proposal,const MCMC &mcmc);
    //void propose_sigma2(Proposal &proposal);
    void propose_rate_one_qtile(Proposal &proposal);
    void propose_rate_one_mtile(Proposal &proposal);
    void propose_overall_mrate(Proposal &proposal);
    void propose_overall_qrate(Proposal &proposal);
    void propose_move_one_qtile(Proposal &proposal);
    void propose_move_one_mtile(Proposal &proposal);
    void propose_birthdeath_qVoronoi(Proposal &proposal);
    void propose_birthdeath_mVoronoi(Proposal &proposal);
    bool accept_proposal(Proposal &proposal);
    
    void print_iteration(const MCMC &mcmc) const;
    void save_iteration(const MCMC &mcmc);
    bool output_results(const MCMC &mcmc) const;
    bool output_current_state() const;
    void check_ll_computation() const;
    string datapath() const;
    string mcmcpath() const;
    string prevpath() const;
    string gridpath() const;
    
    double getMigrationRate(const int edge) const;
    double getCoalescenceRate(const int deme) const;
    void printMigrationAndCoalescenceRates( ) const;
    int revLookup(double i, double j) const;
    
private:
    
    Draw draw; // Random number generator
    Graph graph;
    Params params;
    Habitat habitat;
    
    // Diffs:
    int o; // number of observed demes
    int d; // total number of demes
    int n; // number of samples
    int nstates; // number of states in the structured coalescent CTMC
    int dimKrylov; // number of dimensions in krylov subspace
    MatrixXd totalSharingM; // observed means (for number of IBD blocks)
    MatrixXd cMatrix; // number of pairwise observations between observed populations
    VectorXd cvec; // c is the vector of counts

    
    // Some variables that are used in EEMS_wishpdfln,
    // which might or might not be not useful for EEMS2_wishpdfln
    /*VectorXd cinv;  // cinv is the vector of inverse counts
     VectorXd cmin1;  // cmin1 is the vector of counts - 1
     MatrixXd JtDobsJ;
     MatrixXd JtDhatJ;
     double ldLLt; // logdet(L*L')
     double ldDiQ;  // logdet(inv(Diffs)*Q)
     double ldLDLt;  // logdet(-L*Diffs*L')
     double n_2, logn; int nmin1; // n/2, log(n), n-1
     */
    
    // The current set of parameter values:
    int nowmtiles, nowqtiles; // number of m and q tiles, respectively
    MatrixXd nowmSeeds; VectorXd nowmEffcts; double nowmrateMu; // parameters to describe the m Voronoi tessellation
    MatrixXd nowqSeeds; VectorXd nowqEffcts;                    // parameters to describe the q Voronoi tessellation
    double nowqrateS2, nowmrateS2; // two hyperparameters -- the variance of nowqEffcts and nowmEffcts, respectively
    //double nowsigma2, nowpi, nowll, nowdf; // variance scale, log prior, log likelihood, degrees of freedom
    double nowqrateMu, nowpi, nowll, nowdf; // variance scale, log prior, log likelihood, degrees of freedom
    
    VectorXi nowqColors; // mapping that indicates which q tiles each vertex/deme falls into
    VectorXi nowmColors; // mapping that indicates which m tiles each vertex/deme falls into
    
    // Variables to store the results in:
    // Fixed size:
    MatrixXd mcmcmhyper;
    MatrixXd mcmcqhyper;
    MatrixXd mcmcthetas;
    MatrixXd mcmcpilogl;
    VectorXd mcmcmtiles;
    VectorXd mcmcqtiles;
    // Variable length:
    vector<double> mcmcmRates;
    vector<double> mcmcqRates;
    vector<double> mcmcxCoord;
    vector<double> mcmcyCoord;
    vector<double> mcmcwCoord;
    vector<double> mcmczCoord;
    
    void initialize_sims();
    void randpoint_in_habitat(MatrixXd &Seeds);
    void rnorm_effects(const double mu, const double rateS2, const double upperBound, VectorXd &Effcts);
    
    double eems2_likelihood(const MatrixXd &mSeeds, const VectorXd &mEffcts, const double mrateMu,
                            const MatrixXd &qSeeds, const VectorXd &qEffcts,
                            const double df, const double qrateMu) const;
};

#endif
