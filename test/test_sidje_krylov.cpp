#include <iostream>
using namespace std;
#include "Eigen"
#include "Eigen/Sparse"
using namespace Eigen;
#include <math.h>
#include <fstream>
#include <ctime>
#include <vector>

const int nrow = 10;
const int ncol = 100;
// total number of nodes
const int ndemes = nrow*ncol;

// number of states in the markov chain
// eqn = no of deme pairs + coalescent state
const int nstates = (int) (ndemes*(ndemes+1))/2 + 1;

void swap(int &i, int &j){
    int temp = i;
    i = j;
    j = temp;
}

// a way to go from index in the Q matrix to (i,j)
// excluding the coalescent state
int lookup[nstates-1][2];

int revLookup(double i, double j){
    // subtract the one at the end because matrices in C++ start with 0
    // sum_{k=0}^{i-1) n-k  + sum_{k=i}^{k=j} 1  - 1
 
    // we restrict i <= j
    if (i > j){
        swap(i,j);
    }
    double index = 0;
    if (i >= 1){
        index += 0.5*i*(2.0*ndemes+1-i);
    }
    index += (j-i);
    return ((int) index);
}

struct node {
    vector<int> neighbors;
    int label;
};


double max(double a, double b){
    if (a > b){
        return(a);
    }
    return(b);
}

void padm(MatrixXd &H, MatrixXd &E){
    // REQUIRES: H and E to be both MatrixXd of size NxN.
    // MODIFIES: E
    // EFFECTS: calculates the exponential of H and stores it into E.
    // padm.m from expokit package translated to C++ by Hussein Al-Asadi
    
    
    const int N = H.rows();
    if (N != H.cols() || N != E.rows() || N != E.cols()){
        cout << "ERROR: DIMENSIONS OF INPUT MATRICES ARE INCONSISTENT!" << endl;
    }
    
    // recommended (6,6)-degree rational Pade approximation
    const int p = 6;
    
    // pade coefficients
    VectorXd c(p+1);
    c[0] = 1.0;
    for (int k = 1; k <= p; k++){
        c(k) = c(k-1)*((p+1.0-k)/(k*(2.0*p+1.0-k)));
    }
    
    // L-Infinity norm as defined by norm(H, 'inf') in Matlab
    double s = H.cwiseAbs().rowwise().sum().maxCoeff();
    
    if (s > 0.5){
        s = max(0, floor(log(s)/log(2)) + 2);
        H = pow(2, -s)*H;
    }
    
    // Horner evaluation of the irreducible fraction
    MatrixXd I = MatrixXd::Identity(N,N);
    MatrixXd H2(N,N);
    H2 = H*H;
    MatrixXd Q = c(p)*I;
    MatrixXd P = c(p-1)*I;
    
    int odd = 1;
    for (int k = p-1; k > 0; k--){
        if (odd == 1){
            Q = Q*H2 + c(k-1)*I;
        } else{
            P = P*H2 + c(k-1)*I;
        }
        odd = 1 - odd;
    }
    if (odd == 1){
        Q = Q*H;
        Q = Q-P;
        E = -1*(I + 2*Q.lu().solve(P));
    }
    else{
        P = P*H;
        Q = Q - P;
        E = I + 2*Q.lu().solve(P);
    }
    
    // Squaring
    // loop floor(s) times
    for (int k = 0; k < floor(s); k++){
        E = E*E;
    }
}


void calculateProduct(VectorXd &z, VectorXd &q, const MatrixXd &M, const VectorXd &W, vector<node> nodes) {
    node demei;
    node demej;
    
    // sum is to keep track of the row sum. So then we can fill in the diagonals
    double sum;
    
    // sweeping across the entries of the vector z where z = A*q
    int index = 0;
    
    // going from index to (i,j)
    int state;
    
    for (int i = 0; i < ndemes; i++){
        for (int j = i; j < ndemes; j++){
            demei = nodes[i];
            demej = nodes[j];
            sum = 0.0;
            
            // let i move and fix j since only one step transitions are allowed. Need to look up the migration rate from i to the neighbor of i
            for (int k = 0; k < demei.neighbors.size(); k++){
                sum += M(i,demei.neighbors[k]);
                // we're on the "indexth" row of A and looking at the "stateth" entry of this row
                state = revLookup(demei.neighbors[k], j);
                z[index] += M(i,demei.neighbors[k])*q(state);
            }
            
            // let j move and fix i since only one step transitions are allowed. Need to look up the migration rate from j to the neightbor of j
            for (int k = 0; k < demej.neighbors.size() ; k++){
                sum += M(j,demej.neighbors[k]);
                // we're on the "indexth" row of A and looking at the "stateth" entry of this row
                state = revLookup(i, demej.neighbors[k]);
                z[index] += M(j,demej.neighbors[k])*q(state);
            }
            
            // both i and j coalescece
            if (i == j){
                sum += W(i);
                z[index] += W(i) * q(nstates-1);
            }
            
            // the diagonal
            z[index] -= sum*q(index);
            
            index += 1;
        }
    }
    z[nstates-1] = 0;
}


void krylovProj(MatrixXd &H, MatrixXd &Q, const MatrixXd &M, const VectorXd &W, vector<node> nodes, const int m) {
    // REQUIRES: dimKrylov < nstates
    // MODIFIES: H, Q
    // EFFECTS: krylov projection of the rate matrix,e.g. if  A is rate matrix then
    // finds the decomposition A=Q'HQ
    
    // set up storage
    H.setZero();
    Q.setZero();
    VectorXd z(nstates);
    VectorXd q(nstates);
    
    // initialize first kyrlov basis
    Q(nstates-1, 0) = 1;
    
    // Arnoldi iteration
    for (int k = 1; k < m; k++){
        q = Q.col(k-1);
        z.setZero();
        calculateProduct(z, q, M,W, nodes);
        for (int i = 0; i < k; i++){
            H(i, k-1) = Q.col(i).dot(z);
            z = z - H(i, k-1) * Q.col(i);
        }
        
        H(k, k-1) = z.norm();
        if (H(k,k-1) == 0){
            return;
        }
        Q.col(k) = z / H(k,k-1);
    }
}

// For testing purposes only
// order goes like this for 2 deme model:
// (1,1) -> 0
// (1,2) -> 1
// (2,2) -> 2
// C     -> 3
// Lookup and revLookup is just to go from (i,j) <-> k
void makeFullMatrix(vector<node> &nodes, const MatrixXd &M, const MatrixXd &W, MatrixXd &Q){
    int index;
    node demei;
    node demej;
    int neighbor;
    Q.setZero();
    // i < (nstates-1) because the last row of Q is all zeros
    for (int i = 0; i < (nstates-1); i++){
        demei = nodes[lookup[i][0]];
        demej = nodes[lookup[i][1]];
        
        // fix deme i and look at all the possble demes lineage i can go to (fix lineage j).
        for (int k = 0; k < demei.neighbors.size(); k++){
            neighbor = demei.neighbors[k];
            index = revLookup(neighbor, demej.label);
            Q(i, index) += M(neighbor, demei.label);
        }
        
        for (int k = 0; k < demej.neighbors.size(); k++){
            neighbor = demej.neighbors[k];
            index = revLookup(demei.label, neighbor);
            Q(i, index) += M(neighbor, demej.label);
        }
        
        if (demei.label == demej.label){
            Q(i, nstates-1) += W(demei.label);
        }
        
        Q(i,i) = -1*(Q.row(i).sum());
        
    }
}

void makeSparseMatrix(vector<node> &nodes, const MatrixXd &M, const MatrixXd &W, SparseMatrix<double> &Q){
    int index;
    node demei;
    node demej;
    int neighbor;
    double sum;
    // i < (nstates-1) because the last row of Q is all zeros
    for (int i = 0; i < (nstates-1); i++){
        demei = nodes[lookup[i][0]];
        demej = nodes[lookup[i][1]];
        sum = 0;
        
        // fix deme i and look at all the possble demes lineage i can go to (fix lineage j).
        for (int k = 0; k < demei.neighbors.size(); k++){
            neighbor = demei.neighbors[k];
            index = revLookup(neighbor, demej.label);
            sum += M(neighbor, demei.label);
            Q.coeffRef(i,index) += M(neighbor, demei.label);
        }
        
        for (int k = 0; k < demej.neighbors.size(); k++){
            neighbor = demej.neighbors[k];
            index = revLookup(demei.label, neighbor);
            sum += M(neighbor, demej.label);
            Q.coeffRef(i,index) += M(neighbor, demej.label);

        }
        
        if (demei.label == demej.label){
            sum += W(demei.label);
            Q.coeffRef(i,nstates-1) += W(demei.label);

        }
        
        Q.coeffRef(i,i) -= sum;
        
    }
    Q.makeCompressed();
    
}


void computeWeights(VectorXd &w, VectorXd &x, double r, double L, const int nquad){
    // REQUIRES: w and x vectors of length 30, r is recombination rate, and L (in base pairs) of cutoff.
    // MODIFIES: w and x
    // EFFECTS: x will contain the x-values telling you where to evaluate P(T_mrca = x); w will contains the weights
    // This function allows user to compute an integral by computing \sum_i P(T_mrca = x_i) * w_i
    
    if (nquad == 30){
        x << 0.118440697736960550688, 0.3973475034735802657556, 0.8365549141880933313119, 1.437175158191620443607,
        2.200789508440616292336, 3.129448303166859096349, 4.225699164493802071261, 5.492626704368934083587,
        6.933903364122364597039, 8.553853192793023779194, 10.35753137020864105106, 12.35082332811269876439,
        14.54056869943518703492, 16.93471724415800802837, 19.54252664684054185266, 22.37481610233449499411,
        25.44429563058376261798, 28.76600031447167014762, 32.35787326932856805551, 36.24156497875364752439,
        40.44355691460364227197, 44.99678841355200250088, 49.94309754094208987181, 55.33704611950810443499,
        61.25224904369593075136, 67.79260716731075303985, 75.11420274687672563149, 83.47405073153149030595,
        93.36359463048878316735, 106.0462505962874034422;
        
        w << 0.02093564741472521761, 0.09585049298017654367, 0.18833296435057945936, 0.23281944819987904471,
        0.2060782293528492151, 0.138528960450616358, 0.07293919110208096649, 0.030605607903988887905,
        0.010333948458420042431, 0.002821608083735993584, 6.2402663742264620427E-4, 1.1168849922460852198E-4,
        1.6129719270580565631E-5, 1.87044426274856472768E-6, 1.72995513372709914535E-7, 1.26506996496773906645E-8,
        7.2352574135703022224E-10, 3.19320138447436406004E-11, 1.069761647687436460972E-12, 2.66597906070505518515E-14,
        4.82019019925788439097E-16, 6.12740480626441608041E-18, 5.26125812567892365789E-20, 2.89562589607893296815E-22,
        9.51695437836864011982E-25, 1.69046847745875738033E-27, 1.39738002075239812243E-30, 4.20697826929603166432E-34,
        2.89826026866498969507E-38, 1.411587124593531584E-43;
        
    } else if(nquad == 50){
        
        x << 0.07197890982430907685, 0.2413621356214323113832, 0.50777161206496736682,
        0.87144100848215091489, 1.3327153593876555612, 1.89203857575589673578,
        2.54995389165696269159, 3.30710638809426104509, 4.1642464309382815239,
        5.1222338489683650003, 6.1820428555624776171, 7.3447677682201374241,
        8.611629605039126607, 9.9839836572644003588, 11.4633281577394397878,
        13.0513141887293622298, 14.7497570005632491357, 16.5606489462107181103,
        18.4861742778364391121, 20.5287261015344429523, 22.6909258483688038223,
        24.97564569685792480173, 27.3860344785262187279, 29.92554771997283930857,
        32.59798262998117745422, 35.40751903929353023831, 38.35876755865307164698,
        41.4568265582708885324, 44.7073500182295270902, 48.11662889629256334459,
        51.6916894678465181416, 55.4404132017820128574, 59.37168428037916420825,
        63.49557305617946339417, 67.82356688527364443624, 72.36886439711898866969,
        77.14675619634162666911, 82.17512565948067597586, 87.4751203582245020515,
        93.0720721704444912416, 98.99679073227190311995, 105.2874371482835905836,
        111.9923375735336617652, 119.1743972669017647382, 126.91841438735756732,
        135.344080011584273835, 144.6313615494928725149, 155.0771275144866916574,
        167.2505316308244871017, 182.620207348251479189;
        
        w << 0.008098150669659729617, 0.04130873125538665997, 0.09625940978218537466,
        0.1503491696588344311, 0.17934986299937562831, 0.173581394953721460356,
        0.140856655908618615123, 0.097739526671480248591, 0.0587261120368546837416,
        0.030808796814653968376, 0.014192143542695654478, 0.0057625101944473758948,
        0.0020676112211018121615, 6.566117266315217618E-4, 1.84713846428817139761E-4,
        4.6041662947613873147E-5, 1.016612790318865366769E-5, 1.9870817337963512747E-6,
        3.4344907131572547167E-7, 5.2416191882540332129E-8, 7.050822455756935318E-9,
        8.3415284815468702479E-10, 8.6573754732312158465E-11, 7.8596173841489468557E-12,
        6.2209642223163649024E-13, 4.2769613995502895646E-14, 2.5433779837237564877E-15,
        1.302075024421022199E-16, 5.7083496285476396191E-18, 2.13034598442665506488E-19,
        6.72273042229833935E-21, 1.7803851060592980154E-22, 3.9231584139344725937E-24,
        7.1232728017092643659E-26, 1.05390989562966620855E-27, 1.25438816254046964E-29,
        1.1832996034202271289E-31, 8.693916313933935085E-34, 4.8733576082223977928E-36,
        2.0332444973728568667E-38, 6.12678969991989233E-41, 1.28463695773429860451E-43,
        1.787969839412486007E-46, 1.55365992711589439292E-49, 7.761542881245283525E-53,
        1.984698518925299219E-56, 2.18270822517498151914E-60, 7.7576333601861023782E-65,
        5.1724748561078432042E-70, 1.6224693284923917835E-76;

        
    }else{
        cout << "nquad == 30 or nquad == 50, only" << endl;
        throw std::exception();
    }

    // integral_0^{\inf} 2rte^(-2trL)f(t) = (1/L^2) \integral_0^{\inf} f(u/2rL) ue^(-u)
    w = w*(1/(L*2.0*r*L));
    x = x/(2*r*L);

}

void SidjeApprox(const MatrixXd &M, const VectorXd &W, vector<node> nodes, const int m, VectorXd &times, MatrixXd &Papprox) {
  
    int k1 = 2;
    double btol = 1e-5;
    double mb = m;
    int nstep = 0;
    double tstep;
    double mx;
    double t = (times.tail(1))(0);
    double tnow = 0;
    
    MatrixXd F(m+2, m+2);
    MatrixXd V(nstates, m+1);
    MatrixXd H(m+2, m+2);
    MatrixXd Ht(m+2, m+2);
    V.setZero();
    V(nstates-1, 0) = 1;
    double s;
    VectorXd p(nstates, 1);
    VectorXd w = V.col(0);
    double beta = w.norm();
    SparseMatrix<double> Q(nstates,nstates);
    Q.reserve(VectorXi::Constant(nstates,20));
    makeSparseMatrix(nodes, M, W, Q);

    
    
    while (tnow < t) {
        if (nstep == 0){
            tstep = times[nstep];
        }else{
            tstep = times[nstep] - times[nstep-1];
        }
        V.setZero();
        H.setZero();
        V.col(0) = (1/beta)*w;
        for (int j = 0; j < m; j++){
            p = Q*V.col(j);
            for (int i = 0; i <= j; i++){
                H(i,j) = V.col(i).dot(p);
                p = p - H(i,j)*V.col(i);
            }
            
            s = p.norm();
            if (s < btol & j > 1){
                k1 = 0;
                mb = j;
                break;
            }
            H(j+1, j) = s;
            V.col(j+1) = (1/s)*p;
        }
        
        if (k1 != 0){
            H(m+1, m) = 1;
	}
        mx = mb + k1;
        if (mb < m){
            Ht.resize(mx, mx);
            F.resize(mx, mx);
        }
        Ht = H.block(0, 0, mx, mx);
        Ht = Ht*tstep;
        padm(Ht, F);
        mx = mb + max(0, k1-1);
        w = V.leftCols(mx)*(beta*F.block(0, 0, mx, 1));
        beta = w.norm();
        
        // this actually takes no time
        /*
        int ineg = 0;
        for (int i = 0; i < nstates; i++){
            if (w(i) < 0){
                w(i) = 0;
                ineg += 1;
            }
        }
        if (ineg > 0){
            double wnorm = w.sum();
            w = (1/wnorm)*w;
        }
        */
        
        Papprox.col(nstep) = w;
        tnow += tstep;
        nstep += 1;
    }

 }

void calculateIntegralSidje(const MatrixXd &M, const MatrixXd &W, MatrixXd &lambda, double L, double r, vector<node> &nodes, const int m, const int nquad){
  
    // weights for the gaussian quadrature
    VectorXd w(nquad);
    // abisca for the gaussian quadrature
    VectorXd x(nquad);
    
    computeWeights(w, x, r, L, nquad);
    
    MatrixXd P(nstates, nquad);
    SidjeApprox(M, W, nodes, m, x, P);
    VectorXd p(nquad);

  
    int state;
    for (int i = 0; i < ndemes; i++){
        for (int j = i; j < ndemes; j++)
        {
            state = revLookup(i,j);
            // estimate the probability mass function
            p(0) = 0;
            p.tail(nquad-1) = (P.row(state).tail(nquad-1)- P.row(state).head(nquad-1)).array()/(x.tail(nquad-1)-x.head(nquad-1)).transpose().array();
            // compute the integral
            // 3e9 is genome size
            lambda(i,j) = (3e9)*(w.dot(p));
            //if (lambda(i,j) < 0){
            //    throw std::exception();
            //}
            lambda(j,i) = lambda(i,j);
        }
    }

}



void calculateIntegralKrylov(const MatrixXd &M, const MatrixXd &W, MatrixXd &lambda, double L, double r, vector<node> &nodes, const int m){
    MatrixXd Q(nstates, m);
    MatrixXd H(m, m);
    krylovProj(H, Q, M, W, nodes, m);
    
    // weights for the gaussian quadrature
    VectorXd w(30);
    // abisca for the gaussian quadrature
    VectorXd x(30);
    
    computeWeights(w, x, r, L, 30);
    
    // where to store the matrix exponential
    MatrixXd E(m,m);
    
    // to get the last column
    VectorXd l = VectorXd::Zero(nstates);
    l[nstates-1] = 1.0;
    
    // storing the probabilities
    MatrixXd P(nstates, 30);
    
    MatrixXd Ht(m,m);
    
    for (int i = 0; i < x.size() ; i++){
        Ht = H*x[i];
        padm(Ht, E);
        P.col(i) = (Q*E)*(Q.transpose()*l);
    }
    
    VectorXd p(30);
    int state;
    for (int i = 0; i < ndemes; i++){
        for (int j = i; j < ndemes; j++)
        {
            state = revLookup(i,j);
            // estimate the probability mass function
            p(0) = 0;
            p.tail(29) = (P.row(state).tail(29)- P.row(state).head(29)).array()/(x.tail(29)-x.head(29)).transpose().array();
            // compute the integral
            // 3e9 is genome size
            lambda(i,j) = (3e9)*(w.dot(p));
            //if (lambda(i,j) < 0){
            //    throw std::exception();
            //}
            lambda(j,i) = lambda(i,j);
        }
    }
   
    
}


void calculateIntegral(const MatrixXd &M, const MatrixXd &W, MatrixXd &lambda, double L, double r, vector<node> &nodes){
    VectorXd w(30);
    VectorXd x(30);
    computeWeights(w, x, r, L, 30);

    MatrixXd E(nstates, nstates);
    MatrixXd A(nstates, nstates);
    A.setZero();
    makeFullMatrix(nodes, M, W, A);
    
    //cout << "A: \n" << A << endl;
    
    // to get the last column
    VectorXd l = VectorXd::Zero(nstates);
    l[nstates-1] = 1.0;
    
    MatrixXd At(nstates, nstates);
    MatrixXd P(nstates, 30);
    for (int i = 0; i < x.size() ; i++){
        At = A*x[i];
        padm(At, E);
        P.col(i) = E*l;
    }
    
    VectorXd p(30);
    int state;
    for (int i = 0; i < ndemes; i++){
        for (int j = i; j < ndemes; j++)
        {
            state = revLookup(i,j);
            p(0) = 0;
            p.tail(29) = (P.row(state).tail(29)- P.row(state).head(29)).array()/(x.tail(29)-x.head(29)).transpose().array();
            lambda(i,j) = (3e9)*(w.dot(p));
            lambda(j,i) = lambda(i,j);
        }
    }
    
    
}


void populate_nodes(vector<node> &nodes, MatrixXi &DemePairs){
    // MODIFIES: nodes
    nodes.resize(ndemes);
    
    for (int i = 0; i < ndemes; i++){
        //std::cout << "neighbors of deme " << i <<  " are:";
        //for (unsigned ii=0; ii< nodes[i].neighbors.size(); ii++)
        //    std::cout << ' ' << nodes[i].neighbors[ii];
        //std::cout << '\n';
        nodes[i].neighbors.clear();
    }
    
    for (int i = 0; i < DemePairs.rows(); i++){
        int alpha = DemePairs(i,0);
        int beta = DemePairs(i, 1);
        nodes[alpha].neighbors.push_back(beta);
        nodes[beta].neighbors.push_back(alpha);
        nodes[alpha].label = alpha;
        nodes[beta].label = beta;
    }
}


double poisln(const MatrixXd &Lambda, const MatrixXd &lambda, const MatrixXd &cMatrix){
    double ll = 0;
    int n = Lambda.rows();
    for (int i = 0; i < n; i++){
        for (int j = i; j < n; j++){
            cout << "i: " << i << ", j: " << j << endl;
            ll += lambda(i,j)*log(Lambda(i,j))-cMatrix(i,j)*Lambda(i,j);
        }
    }
    cout << "logl: " << ll << endl;
    return(ll);
}



void get_edge(int edge, int &alpha, int &beta, MatrixXi &DemePairs)
{
    alpha = DemePairs(edge,0); beta = DemePairs(edge,1);
}

void make_edges(int nrow, int ncol, MatrixXi &DemePairs){
    int edge = 0;
    for (int i = 0; i < ndemes; i++){
        
        // if node not on bottom
        if ((i+1) <= ncol*(nrow-1)){
            DemePairs(edge,0) = i;
            DemePairs(edge,1) = i+ncol;
            edge += 1;
        }
        
        // if node not on the right edge
        if (((i+1) % ncol) != 0){
            DemePairs(edge,0) = i;
            DemePairs(edge,1) = i+1;
            edge += 1;
        }
        
        // if node not on the right edge AND not on bottom
        if  ((((i+1) % ncol) != 0)  & ((i+1) <= ncol*(nrow-1)) ){
            DemePairs(edge, 0) = i;
            DemePairs(edge, 1) = i+ncol + 1;
            edge += 1;
        }
    }
}

int main()
{
    // populate lookup array
    int ind = 0;
    for (int i = 0; i < ndemes; i++){
        for (int j=i; j < ndemes; j++){
            lookup[ind][0] = i;
            lookup[ind][1] = j;
            ind += 1;
        }
    }
    int nreps = 50;
    for (int ii = 0; ii < nreps; ii++){
        
        double a = 0;
        double b = 0.1;
        VectorXd mrates(ndemes);
        VectorXd W(ndemes);
        VectorXd ones = VectorXd::Ones(ndemes);
        W.setRandom(ndemes);
        mrates.setRandom(ndemes);
        mrates = (mrates+ones)/2;
        mrates = a*ones + (b-a)*mrates;
        a = -7;
        b = -6.9;
        W = (W+ones)/2;
        W = a*ones + (b-a)*W;
        W = (VectorXd) W.array().exp();
        
        //cout << W << endl;
        //cout << mrates << endl;
        
        // Must agree with ndemes above
        // set up the graph here
        const int nedges = (ncol-1)*nrow + (nrow-1)*ncol + (ncol-1)*(nrow-1);
        MatrixXi DemePairs = MatrixXd::Zero(nedges, 2).cast <int> ();
        
        make_edges(nrow, ncol, DemePairs);
        
        vector<node> nodes;
        
        populate_nodes(nodes, DemePairs);

        MatrixXd M = MatrixXd::Zero(ndemes,ndemes);
        int alpha, beta1;
        for ( int edge = 0 ; edge < nedges ; edge++ ) {
            get_edge(edge,alpha,beta1, DemePairs);
            double m_alpha = mrates(alpha);
            double m_beta = mrates(beta1);
            M(alpha,beta1) = 0.5 * m_alpha + 0.5 * m_beta;
            M(beta1,alpha) = M(alpha,beta1);
        }
        
        SelfAdjointEigenSolver<MatrixXd> es;
        clock_t begin_time = clock();
        es.compute(M);
        cout << float( clock () - begin_time ) /  CLOCKS_PER_SEC << "\n\n" << endl;

        /*
        double L = 4e6;
        double r = 1e-8;
        MatrixXd lambda = MatrixXd::Zero(ndemes, ndemes);
        
        clock_t begin_time = clock();
        calculateIntegralSidje(M, W, lambda, L, r, nodes, 10, 30);
        cout << float( clock () - begin_time ) /  CLOCKS_PER_SEC << "\n\n" << endl;

        cout << lambda.row(0) << "\n\n" << endl;
        
        begin_time = clock();
        calculateIntegralSidje(M, W, lambda, L, r, nodes, 10, 50);
        cout << float( clock () - begin_time ) /  CLOCKS_PER_SEC << "\n\n" << endl;
        cout << lambda.row(0) << "\n" << endl;
        
        cout << "\n\n\n--------------------------\n" << endl;
         */
    }
    
    
}
