/**
 lassosum
 functions.cpp
 Purpose: functions to perform lassosum
 
 @author Timothy Mak
 @author Robert Porsch
 
 @version 0.1
 
 */
// [[Rcpp::interfaces(r, cpp)]]

#include <stdio.h>
#include <string>
#include <bitset>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <RcppArmadillo.h>

// [[Rcpp::depends(RcppArmadillo)]]
using namespace Rcpp;

/**
 Opens a Plink binary files
 
 @s file name
 @BIT ifstream
 @return is plink file in major mode
 
 */

bool openPlinkBinaryFile(const std::string s, std::ifstream &BIT) {
  BIT.open(s.c_str(), std::ios::in | std::ios::binary);
  if (!BIT.is_open()) {
    throw "Cannot open the bed file";
  }
  
  // 2) else check for 0.99 SNP/Ind coding
  // 3) else print warning that file is too old
  char ch[1];
  BIT.read(ch, 1);
  std::bitset<8> b;
  b = ch[0];
  bool bfile_SNP_major = false;
  bool v1_bfile = true;
  // If v1.00 file format
  // Magic numbers for .bed file: 00110110 11011000 = v1.00 bed file
  // std::cerr << "check magic number" << std::endl;
  if ((b[2] && b[3] && b[5] && b[6]) && !(b[0] || b[1] || b[4] || b[7])) {
    // Next number
    BIT.read(ch, 1);
    b = ch[0];
    if ((b[0] && b[1] && b[3] && b[4]) && !(b[2] || b[5] || b[6] || b[7])) {
      // Read SNP/Ind major coding
      BIT.read(ch, 1);
      b = ch[0];
      if (b[0])
        bfile_SNP_major = true;
      else
        bfile_SNP_major = false;
      
      // if (bfile_SNP_major) std::cerr << "Detected that binary PED file is
      // v1.00 SNP-major mode" << std::endl;
      // else std::cerr << "Detected that binary PED file is v1.00
      // individual-major mode" << std::endl;
      
    } else
      v1_bfile = false;
    
  } else
    v1_bfile = false;
  // Reset file if < v1
  if (!v1_bfile) {
    Rcerr << "Warning, old BED file <v1.00 : will try to recover..."
          << std::endl;
    Rcerr << "  but you should --make-bed from PED )" << std::endl;
    BIT.close();
    BIT.clear();
    BIT.open(s.c_str(), std::ios::in | std::ios::binary);
    BIT.read(ch, 1);
    b = ch[0];
  }
  // If 0.99 file format
  if ((!v1_bfile) && (b[1] || b[2] || b[3] || b[4] || b[5] || b[6] || b[7])) {
    Rcerr << std::endl
          << " *** Possible problem: guessing that BED is < v0.99      *** "
          << std::endl;
    Rcerr << " *** High chance of data corruption, spurious results    *** "
          << std::endl;
    Rcerr
      << " *** Unless you are _sure_ this really is an old BED file *** "
      << std::endl;
    Rcerr << " *** you should recreate PED -> BED                      *** "
          << std::endl
          << std::endl;
    bfile_SNP_major = false;
    BIT.close();
    BIT.clear();
    BIT.open(s.c_str(), std::ios::in | std::ios::binary);
  } else if (!v1_bfile) {
    if (b[0])
      bfile_SNP_major = true;
    else
      bfile_SNP_major = false;
    Rcerr << "Binary PED file is v0.99" << std::endl;
    if (bfile_SNP_major)
      Rcerr << "Detected that binary PED file is in SNP-major mode"
            << std::endl;
      else
        Rcerr << "Detected that binary PED file is in individual-major mode"
              << std::endl;
  }
  return bfile_SNP_major;
}

//' Count number of lines in a text file
//' 
//' @param fileName Name of file
//' @keywords internal
//' 
// [[Rcpp::export]]
int countlines(const char* fileName) {
  
  // Stolen from http://stackoverflow.com/questions/3482064/counting-the-number-of-lines-in-a-text-file
  int number_of_lines = 0;
  std::string line;
  std::ifstream myfile(fileName);
  
  while (std::getline(myfile, line))
    ++number_of_lines;
  return number_of_lines;
}

//' Multiply genotypeMatrix by a matrix
//' 
//' @param fileName location of bam file
//' @param N number of subjects 
//' @param P number of positions 
//' @param input the matrix
//' @param col_skip_pos which variants should we skip
//' @param col_skip which variants should we skip
//' @param keepbytes which bytes to keep
//' @param keepoffset what is the offset
//' @return an armadillo genotype matrix 
//' @keywords internal
//' 
// [[Rcpp::export]]
arma::mat multiBed3(const std::string fileName, int N, int P, const arma::mat input, 
                    arma::Col<int> col_skip_pos, arma::Col<int> col_skip, 
                    arma::Col<int> keepbytes, arma::Col<int> keepoffset, 
                    const int trace) {
  
  std::ifstream bedFile;
  bool snpMajor = openPlinkBinaryFile(fileName, bedFile);
  if (!snpMajor)
    throw std::runtime_error("We currently have no plans of implementing the "
                               "individual-major mode. Please use the snp-major "
                               "format");
  
  int i = 0;
  int ii = 0;
  int iii = 0;
  const bool colskip = (col_skip_pos.n_elem > 0);
  unsigned long long int Nbytes = ceil(N / 4.0);
  const bool selectrow = (keepbytes.n_elem > 0);
  int n;
  if (selectrow)
    n = keepbytes.n_elem;
  else
    n = N;
  int jj;
  
  arma::mat result = arma::mat(n, input.n_cols, arma::fill::zeros);
  std::bitset<8> b; // Initiate the bit array
  char ch[Nbytes];
  
  int chunk;
  double step;
  double Step = 0; 
  if(trace > 0) {
    chunk = input.n_rows / pow(10, trace); 
    step = 100 / pow(10, trace); 
    // Rcout << "Started C++ program \n"; 
  }
  
  while (i < P) {
    Rcpp::checkUserInterrupt();
    if (colskip) {
      if (ii < col_skip.n_elem) {
        if (i == col_skip_pos[ii]) {
          bedFile.seekg(col_skip[ii] * Nbytes, bedFile.cur);
          i = i + col_skip[ii];
          ii++;
          continue;
        }
      }
    }
    
    if(trace > 0) {
      if (iii % chunk == 0) {
        Rcout << Step << "% done\n";
        Step = Step + step; 
      }
    }
    
    bedFile.read(ch, Nbytes); // Read the information
    if (!bedFile)
      throw std::runtime_error(
          "Problem with the BED file...has the FAM/BIM file been changed?");
    
    int j = 0;
    if (!selectrow) {
      for (jj = 0; jj < Nbytes; jj++) {
        b = ch[jj];
        
        int c = 0;
        while (c < 7 && j < N) { // from the original PLINK: 7 because of 8 bits
          int first = b[c++];
          int second = b[c++];
          if (first == 0) {
            for (int k = 0; k < input.n_cols; k++) {
              if (input(iii, k) != 0.0) {
                result(j, k) += (2 - second) * input(iii, k);
              }
            }
          }
          j++;
        }
      }
    } else {
      for (jj = 0; jj < keepbytes.n_elem; jj++) {
        b = ch[keepbytes[jj]];
        
        int c = keepoffset[jj];
        int first = b[c++];
        int second = b[c];
        if (first == 0) {
          for (int k = 0; k < input.n_cols; k++) {
            if (input(iii, k) != 0.0) {
              result(j, k) += (2 - second) * input(iii, k);
            }
          }
        }
        j++;
      }
    }
    
    i++;
    iii++;
  }
  
  return result;
}


//' Multiply genotypeMatrix by a matrix (sparse)
//' 
//' @param fileName location of bam file
//' @param N number of subjects 
//' @param P number of positions 
//' @param input the matrix
//' @param col_skip_pos which variants should we skip
//' @param col_skip which variants should we skip
//' @param keepbytes which bytes to keep
//' @param keepoffset what is the offset
//' @return an armadillo genotype matrix 
//' @keywords internal
//' 
// [[Rcpp::export]]
arma::mat multiBed3sp(const std::string fileName, int N, int P, 
                      const arma::vec beta, 
                      const arma::Col<int> nonzeros, 
                      const arma::Col<int> colpos,
                      const int ncol, 
                      arma::Col<int> col_skip_pos, arma::Col<int> col_skip, 
                      arma::Col<int> keepbytes, arma::Col<int> keepoffset, 
                      const int trace) {
  
  std::ifstream bedFile;
  bool snpMajor = openPlinkBinaryFile(fileName, bedFile);
  if (!snpMajor)
    throw std::runtime_error("We currently have no plans of implementing the "
                               "individual-major mode. Please use the snp-major "
                               "format");
  
  int i = 0;
  int ii = 0;
  int iii = 0;
  int k = 0;
  const bool colskip = (col_skip_pos.n_elem > 0);
  unsigned long long int Nbytes = ceil(N / 4.0);
  const bool selectrow = (keepbytes.n_elem > 0);
  int n;
  if (selectrow)
    n = keepbytes.n_elem;
  else
    n = N;
  int jj;
  
  arma::mat result = arma::mat(n, ncol, arma::fill::zeros);
  std::bitset<8> b; // Initiate the bit array
  char ch[Nbytes];
  
  int chunk;
  double step;
  double Step = 0; 
  if(trace > 0) {
    chunk = nonzeros.n_elem / pow(10, trace); 
    step = 100 / pow(10, trace); 
    // Rcout << "Started C++ program \n"; 
  }
  
  while (i < P) {
    Rcpp::checkUserInterrupt();
    if (colskip) {
      if (ii < col_skip.n_elem) {
        if (i == col_skip_pos[ii]) {
          bedFile.seekg(col_skip[ii] * Nbytes, bedFile.cur);
          i = i + col_skip[ii];
          ii++;
          continue;
        }
      }
    }
    
    if(trace > 0) {
      if (iii % chunk == 0) {
        Rcout << Step << "% done\n";
        Step = Step + step; 
      }
    }
    
    bedFile.read(ch, Nbytes); // Read the information
    if (!bedFile)
      throw std::runtime_error(
          "Problem with the BED file...has the FAM/BIM file been changed?");
    
    int j = 0;
    if (!selectrow) {
      for (jj = 0; jj < Nbytes; jj++) {
        b = ch[jj];
        
        int c = 0;
        while (c < 7 && j < N) { // from the original PLINK: 7 because of 8 bits
          int first = b[c++];
          int second = b[c++];
          if(nonzeros[iii] > 0) {
            if (first == 0) {
              for (int kk = 0; kk < nonzeros[iii]; kk++) {
                result(j, colpos[k]) += (2 - second) * beta[k];
                k++;
              }
              k -= nonzeros[iii];
            }
          }
          j++;
        }
      }
    } else {
      for (jj = 0; jj < keepbytes.n_elem; jj++) {
        b = ch[keepbytes[jj]];
        
        int c = keepoffset[jj];
        int first = b[c++];
        int second = b[c];
        if(nonzeros[iii] > 0) {
          if (first == 0) {
            for (int kk = 0; kk < nonzeros[iii]; kk++) {
              result(j, colpos[k]) += (2 - second) * beta[k];
              k++;
            }
            k -= nonzeros[iii];
          }
        }
        j++;
      }
    }
    
    k += nonzeros[iii];
    i++;
    iii++;
  }
  
  return result;
}



//' Performs elnet
//'
//' @param lambda1 lambda
//' @param lambda2 lambda
//' @param X genotype Matrix
//' @param r correlations
//' @param x beta coef
//' @param thr threshold 
//' @param yhat A vector
//' @param trace if >1 displays the current iteration
//' @param maxiter maximal number of iterations
//' @return conv
//' @keywords internal
//' 
// [[Rcpp::export]]
int elnet(double lambda1, double lambda2, double lambda_ct, const arma::vec& diag, const arma::mat& X, 
          const arma::mat& r, double thr, arma::vec& x, arma::vec& yhat, int trace, int maxiter)
{
  
  // diag is basically diag(X'X)
  // Also, ensure that the yhat=X*x in the input. Usually, both x and yhat are preset to 0. 
  // They are modified in place in this function. 
  
  int n=X.n_rows;
  int p=X.n_cols;
  int traits=r.n_cols;
  
  if(r.n_rows != p) stop("r.n_rows != p");
  if(x.n_elem != p*traits) stop("x.n_elem != p*traits");
  if(yhat.n_elem != n*traits) stop("yhat.n_elem != n*traits");
  if(diag.n_elem != p) stop("diag.n_elem != p"); 
  
  double dlx,del,t,xj,ctp;
  int j,i;

  arma::vec Lambda2(p); 
  Lambda2.fill(lambda2);
  arma::vec Lambda_ct(p); 
  Lambda_ct.fill((traits-1)*lambda_ct);
  
  arma::vec denom=diag + Lambda2 + Lambda_ct;
  
  int conv=0;
  for(int k=0;k<maxiter ;k++) {
    dlx=0.0;
    for(int tt=0; tt<traits;tt++){
      for(j=0; j < p; j++) {
        xj=x(tt*p+j);
        x(tt*p+j)=0.0;
        t= diag(j) * xj + r(j,tt) - arma::dot(X.col(j), yhat.subvec(tt*n, (tt+1)*n-1));
        // Think of it as r(j) - (dotproduct(X.col(j), yhat) - diag(j)*xj)
        ctp=0.0;
        for(int ttt=0;ttt<traits;ttt++){
          if(ttt!=tt){
            ctp+=x(ttt*p+j);
          }
          ctp*=lambda_ct;
        }
        if(std::abs(t+ctp)-lambda1 > 0.0){
          if(t+ctp-lambda1 > 0.0){
            x(tt*p+j)=t-lambda1+ctp/denom(j);
          } else{
            x(tt*p+j)=t+lambda1+ctp/denom(j);
          }
        } 
        // abs(t)-lambda1 > 0 => (t > 0 && t > lambda1) || (t < 0 && t < -lambda1)
        // In either case, there's shrinkage, but not to zero
        // If (t > 0 && t < lambda1) || (t < 0 && t > -lambda1), 0 is the minimum
        
        if(x(tt*p+j)==xj) continue;
        del=x(tt*p+j)-xj;   // x(tt*p+j) is new, xj is old
        dlx=std::max(dlx,std::abs(del));
        
        yhat.subvec(tt*n, (tt+1)*n-1) += del*X.col(j);
      } 
    }
    checkUserInterrupt();
    if(trace > 0) Rcout << "Iteration: " << k << "\n";
    
    if(dlx < thr) {
      conv=1;
      break;
    }
  }
  return conv;
}

// [[Rcpp::export]]
int repelnet(double lambda1, double lambda2, double lambda_ct, arma::vec& diag, arma::mat& X, arma::mat& r,
             double thr, arma::vec& x, arma::vec& yhat, int trace, int maxiter, 
             arma::Col<int>& startvec, arma::Col<int>& endvec)
{
  
  // Repeatedly call elnet by blocks...
  int nreps=startvec.n_elem;
  int out=1;
  int traits = r.n_cols;
  int n = X.n_rows;
  for(int i=0;i < startvec.n_elem; i++) {
    int len_single_trait = endvec(i)-startvec(i)+1;
    arma::vec xtouse(len_single_trait*traits);
    arma::vec yhattouse(n*traits);
    for(int ii=0; ii<traits; ii++){
      xtouse.subvec(len_single_trait*ii,len_single_trait*(ii+1)-1) = x.subvec(startvec(i)+ii*X.n_cols, endvec(i)+ii*X.n_cols);
      yhattouse.subvec(n*ii,n*(ii+1)-1) = X.cols(startvec(i), endvec(i)) * xtouse.subvec(len_single_trait*ii,len_single_trait*(ii+1)-1);
    }
    
    int out2=elnet(lambda1, lambda2, lambda_ct,
                   diag.subvec(startvec(i), endvec(i)), 
                   X.cols(startvec(i), endvec(i)), 
                   r.rows(startvec(i), endvec(i)),
                   thr, xtouse, 
                   yhattouse, trace - 1, maxiter);
    for(int ii=0; ii<traits; ii++){
      x.subvec(startvec(i)+ii*X.n_cols, endvec(i)+ii*X.n_cols) = xtouse.subvec(len_single_trait*ii,len_single_trait*(ii+1)-1);
    }
    yhat += yhattouse; //?
    if(trace > 0) Rcout << "Block: " << i << "\n";
    out=std::min(out, out2);
  }
  return out; 
}

//' imports genotypeMatrix
//' 
//' @param fileName location of bam file
//' @param N number of subjects 
//' @param P number of positions 
//' @param col_skip_pos which variants should we skip
//' @param col_skip which variants should we skip
//' @param keepbytes which bytes to keep
//' @param keepoffset what is the offset
//' @return an armadillo genotype matrix 
//' @keywords internal
//' 
// [[Rcpp::export]]
arma::mat genotypeMatrix(const std::string fileName, int N, int P,
                         arma::Col<int> col_skip_pos, arma::Col<int> col_skip, 
                         arma::Col<int> keepbytes, arma::Col<int> keepoffset, 
                         const int fillmissing) {
  
  std::ifstream bedFile;
  bool snpMajor = openPlinkBinaryFile(fileName, bedFile);
  
  if (!snpMajor)
    throw std::runtime_error("We currently have no plans of implementing the "
                               "individual-major mode. Please use the snp-major "
                               "format");
  
  int i = 0;
  int ii = 0;
  const bool colskip = (col_skip_pos.n_elem > 0);
  unsigned long long int Nbytes = ceil(N / 4.0);
  const bool selectrow = (keepbytes.n_elem > 0);
  int n, p, nskip;
  if (selectrow)
    n = keepbytes.n_elem;
  else
    n = N;
  
  if (colskip) {
    nskip = arma::accu(col_skip);
    p = P - nskip;
  }  else
    p = P;
  
  int j, jj, iii;
  
  arma::mat genotypes = arma::mat(n, p, arma::fill::zeros);
  std::bitset<8> b; // Initiate the bit array
  char ch[Nbytes];
  
  iii=0;
  while (i < P) {
    // Rcout << i << std::endl;
    Rcpp::checkUserInterrupt();
    if (colskip) {
      if (ii < col_skip.n_elem) {
        if (i == col_skip_pos[ii]) {
          bedFile.seekg(col_skip[ii] * Nbytes, bedFile.cur);
          i = i + col_skip[ii];
          ii++;
          continue;
        }
      }
    }
    
    bedFile.read(ch, Nbytes); // Read the information
    if (!bedFile)
      throw std::runtime_error(
          "Problem with the BED file...has the FAM/BIM file been changed?");
    
    j = 0; 
    if (!selectrow) {
      for (jj = 0; jj < Nbytes; jj++) {
        b = ch[jj];
        
        int c = 0;
        while (c < 7 &&
               j < N) { // from the original PLINK: 7 because of 8 bits
          int first = b[c++];
          int second = b[c++];
          if (first == 0) {
            genotypes(j, iii) = (2 - second);
          }
          if(fillmissing == 0 && first == 1 && second == 0) genotypes(j, iii) =arma::datum::nan;
          j++;
        }
      }
    } else {
      for (jj = 0; jj < keepbytes.n_elem; jj++) {
        b = ch[keepbytes[jj]];
        
        int c = keepoffset[jj];
        int first = b[c++];
        int second = b[c];
        if (first == 0) {
          genotypes(j, iii) = (2 - second);
        }
        if(fillmissing == 0 && first == 1 && second == 0) genotypes(j, iii) =arma::datum::nan;
        j++;
      }
    }
    i++;
    iii++; 
  }
  return genotypes;
}


//' normalize genotype matrix
//' 
//' @param genotypes a armadillo genotype matrix
//' @return standard deviation
//' @keywords internal
//' 
// [[Rcpp::export]]
arma::vec normalize(arma::mat &genotypes)
{
  int k = genotypes.n_cols;
  int n = genotypes.n_rows;
  arma::vec sd(k);
  for (int i = 0; i < k; ++i) {
    double m = arma::mean(genotypes.col(i));
    arma::vec mm(n); mm.fill(m);
    sd(i) = arma::stddev(genotypes.col(i));
    // sd(i) = 1.0;
    genotypes.col(i) = arma::normalise(genotypes.col(i) - mm);
  }
  return sd; 
}

//' Runs elnet with various parameters
//' 
//' @param lambda1 a vector of lambdas (lambda2 is 0)
//' @param lambda_ct a double for ctp
//' @param fileName the file name of the reference panel
//' @param r a vector of correlations
//' @param N number of subjects
//' @param P number of position in reference file
//' @param col_skip_posR which variants should we skip
//' @param col_skipR which variants should we skip
//' @param keepbytesR required to read the PLINK file
//' @param keepoffsetR required to read the PLINK file
//' @param thr threshold
//' @param x a numeric vector of beta coefficients
//' @param trace if >1 displays the current iteration
//' @param maxiter maximal number of iterations
//' @param Constant a constant to multiply the standardized genotype matrix
//' @return a list of results
//' @keywords internal
//'  
// [[Rcpp::export]]
List runElnet(arma::vec& lambda, double shrink, double lambda_ct, const std::string fileName,
              arma::mat& r, int N, int P, 
              arma::Col<int>& col_skip_pos, arma::Col<int>& col_skip, 
              arma::Col<int>& keepbytes, arma::Col<int>& keepoffset, 
              double thr, arma::vec& x, int trace, int maxiter, 
              arma::Col<int>& startvec, arma::Col<int>& endvec) {
  // a) read bed file
  // b) standardize genotype matrix
  // c) multiply by constatant factor
  // d) perfrom elnet
  
  // Rcout << "ABC" << std::endl;
  int i,j;
  int traits = r.n_cols;
  arma::mat genotypes = genotypeMatrix(fileName, N, P, col_skip_pos, col_skip, keepbytes,
                                       keepoffset, 1);
  //Rcout << "DEF" << std::endl;
  int p = genotypes.n_cols;
  if (genotypes.n_cols != r.n_rows) {
    throw std::runtime_error("Number of positions in reference file is not "
                               "equal the number of regression coefficients");
  }
  
  arma::vec sd = normalize(genotypes);
  
  genotypes *= sqrt(1.0 - shrink);
  
  arma::Col<int> conv(lambda.n_elem);
  int len = r.n_rows * traits;
  
  arma::mat beta(len, lambda.n_elem);
  arma::mat pred(genotypes.n_rows * traits, lambda.n_elem); pred.zeros();
  arma::vec out(lambda.n_elem);
  arma::vec loss(lambda.n_elem);
  arma::vec diag(r.n_rows); diag.fill(1.0 - shrink); 
  // Rcout << "HIJ" << std::endl;
  
  for(j=0; j < diag.n_elem; j++) {
    if(sd(j) == 0.0) diag(j) = 0.0;
  }
  // Rcout << "LMN" << std::endl;
  
  arma::vec fbeta(lambda.n_elem);
  arma::vec yhat(genotypes.n_rows * traits);
  // yhat = genotypes * x;
  
  
  // Rcout << "Starting loop" << std::endl;
  for (i = 0; i < lambda.n_elem; ++i) {
    if (trace > 0)
      Rcout << "lambda: " << lambda(i) << "\n" << std::endl;
    out(i) =
      repelnet(lambda(i), shrink, lambda_ct, diag,genotypes, r, thr, x, yhat, trace-1, maxiter, 
               startvec, endvec);
    beta.col(i) = x;
    for(j=0; j < r.n_rows; j++) {
      if(sd(j) == 0.0) {
        for(int tt=0; tt<traits; tt++){
          beta(tt*r.n_rows+j,i) *= shrink;
        }
      }
    }
    //if (out(i) != 1) {
    //  throw std::runtime_error("Not converging.....");
    //}
    pred.col(i) = yhat;
    loss(i) = arma::as_scalar(arma::sum(arma::pow(yhat, 2)) -
      2.0 * arma::sum(x % r.as_col()));    //need change
    fbeta(i) =
      arma::as_scalar(loss(i) + 2.0 * arma::sum(arma::abs(x)) * lambda(i) +
      arma::sum(arma::pow(x, 2)) * shrink);
    for(int tt=0; tt<traits; tt++){
      for(int ttt=tt+1; ttt<traits;ttt++){
        fbeta(i)+=(lambda_ct*arma::sum(arma::pow(x.subvec(tt*p,(tt+1)*p-1)-x.subvec(ttt*p,(ttt+1)*p-1),2)));
      }
    }
  }
  return List::create(Named("lambda") = lambda, 
                      Named("beta") = beta,
                      Named("conv") = out,
                      Named("pred") = pred,
                      Named("loss") = loss, 
                      Named("fbeta") = fbeta, 
                      Named("sd")= sd);
}

