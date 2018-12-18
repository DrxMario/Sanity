#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <fstream>
#include <omp.h>

#include <FitFrac.h>
#include <Digamma_Trigamma.h>

string VERSION("1.0");

using namespace std;

/***Function declarations ****/
void get_gene_expression_level(double *n_c, double *N_c, double n, double vmin, double vmax, double &mu, double &var_mu, double *delta, double *var_delta, int C, int numbin, double a, double b, double *lik);
void parse_argv(int argc,char** argv, string &in_file, string &out_folder, int &N_threads, string &extended_output);
static void show_usage(string name);

int main (int argc, char** argv){
	
	string in_file;
	string out_folder;
	int N_threads;
	string extended_output("false");
	parse_argv(argc, argv, in_file, out_folder, N_threads, extended_output);

	bool print_extended_output(false);
	if ( extended_output == "true" || extended_output == "1" )
		print_extended_output = true;

    int g, i, k;
    int G_tmp, C;
    char ss[3000000],sc[3000000];

    FILE *infp; 
    infp = (FILE *) fopen(in_file.c_str(),"r");
    if(infp == NULL){
        fprintf(stderr,"Cannot open input file %s\n",in_file.c_str());
        exit(EXIT_FAILURE);
    }

    /***First line should have the names of the columns (sample names)***/
    fgets(ss,3000000,infp);
    strcpy(sc,ss);
    char *token = strtok(ss," \t");

    C = 0;
    while(token){
        ++C;
        token = strtok(NULL," \t");
    }
    C = C-1;
    printf("There were %d cells\n",C);

    /***now go fill in the character names***/
	string cell_names [C+1];
    token = strtok(sc," \t");
    i=0;
	int tmp;
    while(token){
		cell_names[i] = string(token);
		tmp = cell_names[i].find('\n');
		if (tmp > -1){
			cell_names[i].erase(tmp,1);
		}
        ++i;
        token = strtok(NULL," \t");
    }

    G_tmp = 0;
    while( fgets(ss,3000000,infp)){
        ++G_tmp;
    }
    fclose(infp);
    printf("There were %d rows\n",G_tmp);

	// Count per gene and cell
    double **n_c_tmp = new double *[G_tmp];
    for(g=0;g<G_tmp;++g){
        n_c_tmp[g] = new double [C];
	}
	//double *N_c = new double [C];
	double *N_c = new double [C];
    for(i=0;i<C;++i){
        N_c[i] = 0;
    }

	// Total Count per gene :
	double *n_tmp = new double [G_tmp];
    for(g=0;g<G_tmp;++g){
        n_tmp[g] = 0;
    }

	// Get index of genes with counts bigger than 0
	int *idx = new int [G_tmp];
	int G(0);

	string gene_names_tmp [G_tmp];
    /**reopen file to read the counts***/
    infp = (FILE *) fopen(in_file.c_str(),"r");
    /***first line has names***/
    fgets(ss,3000000,infp);
    char *retval;
    for(g=0; g<G_tmp; ++g){
        retval = fgets(ss,3000000,infp);
        if(retval == NULL){
            fprintf(stderr,"Error: Couldn't read a line at row number %d\n",g);
            return (0);
        }
        /***cut the line based on spaces/tabs***/
        strcpy(sc,ss);
        token = strtok(ss," \t");
        gene_names_tmp[g] = token;
		n_tmp[g] = 0;
        for(i=0; i<C; ++i){
            /****go to the next in the list of words****/
            token = strtok(NULL," \t");
            if(token != NULL){
                n_c_tmp[g][i] = atoi(token);
				n_tmp[g] += n_c_tmp[g][i];/**total count this gene**/
                N_c[i] += n_c_tmp[g][i];// total count for this cell
            }else{
                fprintf(stderr,"Error: not enough fields on line number %d:\n%s\n",g,sc);
                return (0);
            }
        }
		if (n_tmp[g] > 0){
			idx[G++] = g;
		}
        token = strtok(NULL," \t");
        if(token != NULL){
            fprintf(stderr,"Error: too many fields on line number %d:\n%s\n",g,sc);
        }
    }
    fclose(infp);

	// Now get  gene_names, n_c and n for expressed genes only
    printf("There were %d genes expressed\n",G);

    double **n_c = new double *[G];
    for(g=0;g<G;++g){
        n_c[g] = new double [C];
	}
	double *n = new double [G];
	string gene_names [G];

	for(g=0; g<G; ++g){
		n[g] = n_tmp[idx[g]];
		gene_names[g] = gene_names_tmp[idx[g]];
		for(i=0; i<C; ++i){
			n_c[g][i] = n_c_tmp[idx[g]][i];
		}
	}
	delete[] n_tmp;
	delete[] n_c_tmp;

	// Compute output with best v
	double *mu = new double [G];
	double *var_mu = new double [G];
	double **delta = new double *[G];
	double **var_delta = new double *[G];
	for( g=0; g<G; ++g){
		delta[g] = new double [C];
		var_delta[g] = new double [C];
	}

	double vmin = 0.01;
    double vmax = 20.0;
	int numbin = 116;
	double deltav = log(vmax/vmin)/((double) numbin-1);
    /*** Declare output variable Likelihood ***/
	double **lik = new double *[G];
	for(g=0;g<G;g++){
		lik[g] = new double [numbin];
		for(k=0;k<numbin;k++)
			lik[g][k] = -1.0; 
	}

	// alpha and beta of gamma prior on mu
	double a;
	double b;
	a = 1.0;
	b = 0.0;
	cout << "Gamma prior on mu:\na=" << a << "\tb=" << b << "\n";

   	// Get Loglikelihood :
	cout << "Fit gene expression levels\n";
	#pragma omp parallel for num_threads(N_threads)
    for(g=0;g<G;++g){
		get_gene_expression_level(n_c[g],N_c,n[g],vmin,vmax,mu[g],var_mu[g],delta[g],var_delta[g],C,numbin,a,b,lik[g]);
	}
	

	// Write output files	

	cout << "Print output\n";
	// Write log expression table and error bars table
	ofstream out_exp_lev, out_d_exp_lev;
	out_exp_lev.open(out_folder + "/expression_level.txt",ios::out);
	out_d_exp_lev.open(out_folder + "/d_expression_level.txt",ios::out);

	out_exp_lev << "GeneID";
	out_d_exp_lev << "GeneID";
    for(i=1;i<C+1;i++){
		out_exp_lev << "\t" << cell_names[i].c_str();
		out_d_exp_lev << "\t" << cell_names[i].c_str();
	}
	out_exp_lev << "\n";
	out_d_exp_lev << "\n";

	for(g=0;g<G;g++){
	out_exp_lev << gene_names[g].c_str();
	out_d_exp_lev << gene_names[g].c_str();
		for(i=0;i<C;i++){
			out_exp_lev << "\t" << mu[g] + delta[g][i];
			out_d_exp_lev << "\t" << sqrt( var_mu[g] + var_delta[g][i] );
		}
		if ( g != G-1 ){
			out_exp_lev << "\n";
			out_d_exp_lev << "\n";
		}
	}
	out_exp_lev.close();
	out_d_exp_lev.close();

	if ( print_extended_output ){
		cout << "Print output\n";
		string my_file;
		FILE *out_gene, *out_cell, *out_mu, *out_dmu, *out_delta, *out_ddelta;
		// output files
		my_file = out_folder + "/geneID.txt";
		out_gene = (FILE *) fopen(my_file.c_str(),"w");
		if(out_gene == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		my_file = out_folder + "/cellID.txt";
		out_cell = (FILE *) fopen(my_file.c_str(),"w");
		if(out_cell == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		my_file = out_folder + "/mu.txt";
		out_mu = (FILE *) fopen(my_file.c_str(),"w");
		if(out_mu == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		my_file = out_folder + "/d_mu.txt";
		out_dmu = (FILE *) fopen(my_file.c_str(),"w");
		if(out_dmu == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		my_file = out_folder + "/delta.txt";
		out_delta = (FILE *) fopen(my_file.c_str(),"w");
		if(out_delta == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		my_file = out_folder + "/d_delta.txt";
		out_ddelta = (FILE *) fopen(my_file.c_str(),"w");
		if(out_ddelta == NULL){
			fprintf(stderr,"Cannot open output file %s\n",my_file.c_str());
			exit(EXIT_FAILURE);
		}

		// output cell name
		for(i=1;i<C;i++){
			fprintf(out_cell,"%s\n",cell_names[i].c_str());
		}
		fprintf(out_cell,"%s",cell_names[C].c_str());
		for(g=0; g<G; ++g){
			// Write gene names
			fprintf(out_gene,"%s\n",gene_names[g].c_str());

			//print best fit to file : mu, delta
			// Print diagonal of invM : variance of mu, delta
			fprintf(out_mu,"%lf\n",mu[g]);
			fprintf(out_dmu,"%lf\n",sqrt(var_mu[g]));
			for(i=0;i<C-1;++i){
				fprintf(out_delta,"%lf\t",delta[g][i]);
				fprintf(out_ddelta,"%lf\t",sqrt(var_delta[g][i]));
			}
			fprintf(out_delta,"%lf\n",delta[g][C-1]);
			fprintf(out_ddelta,"%lf\n",sqrt(var_delta[g][C-1]));
		}
		fclose(out_gene);
		fclose(out_cell);
		fclose(out_mu);
		fclose(out_dmu);
		fclose(out_delta);
		fclose(out_ddelta);

		// Write likelihood
		cout << "Print likelihood\n";
		ofstream out_lik;
		out_lik.open(out_folder + "/likelihood.txt",ios::out);
		// write v values of bins in loglikelihood
		double v;
		out_lik << "Variance\t";
		for(k=0;k<(numbin-1);++k){
			v = vmin*exp(deltav*k);
			out_lik << v << "\t";
		}
		v = vmin*exp(deltav*(numbin-1));
		out_lik << v << "\n";
		for(g=0; g<G; ++g){
			out_lik << gene_names[g] << "\t";
			for(k=0;k<numbin;++k){
				if(k<numbin-1){
					out_lik << lik[g][k] << "\t";
				}else{
					out_lik << lik[g][k] << "\n";
				}
			}
		}
		out_lik.close();
	}

    return 0;
}

void get_gene_expression_level(double *n_c, double *N_c, double n, double vmin, double vmax, double &mu, double &var_mu, double *delta, double *var_delta, int C, int numbin, double a, double b, double *lik){
	int i, k;
    double beta,L,ldet,q,delsq,inv_v;
	double *f = new double[C];
	double **delta_v = new double *[numbin];
    double **d_delta_v = new double *[numbin];
    for(k=0;k<numbin;++k){
        delta_v[k] = new double [C];
        d_delta_v[k] = new double [C];
    }

	double *mu_v = new double[numbin];
	double Lmax = -1000000000.0;
    double v;
	double deltav;
    deltav = log(vmax/vmin)/((double) numbin-1);

	/*** Compute to compute var of delta ***/
    double d_delta_c [C];
    double d_delta_num [C];
    double d_delta_den2 [C];
    double d_delta_den1;

	for(k=0;k<numbin;++k){
		v = vmin * exp(deltav*k);
		beta = 1.0/((n+a)*v);
		//#pragma omp critical
		q = fitfrac(f,n_c,n,v,C,N_c,a,b);

		mu_v[k] = Psi_0(n+a)-q; /*** equation (85) ***/

		double tot = 0;
		for(i=0;i<C;++i){
			tot += f[i];
		}
		delsq = 0;
		L = -0.5*((double) C)*log(v); // (56) 1st term
		for(i=0;i<C;++i){
			delta_v[k][i] = log(f[i]) - log(N_c[i]) + q; // equation (67)
			L += ( n_c[i]+a)*delta_v[k][i];//3rd term in equation (56)
			delsq += delta_v[k][i]*delta_v[k][i];
		}
		L -= delsq/(2*v);// (56) 2nd term
		L -= (n+a)*q;//4th term in equation (56)

		// get the determinant of the matrix
		ldet = 0.0;
		for(i=0;i<C;++i){
			ldet += (f[i]*f[i])/(f[i]+beta);
		}

		ldet = log(1 - ldet);
		for(i=0;i<C;++i){
			ldet += log(f[i]+beta);
		}
		L -= 0.5*ldet;
		// substract prior with a = 1, b = 1 ( log(v^a*exp(-b*v)) = alog(v) - bv
		lik[k] = L;

		if(L > Lmax){
			Lmax = L;
		}

		/* compute nf^2/(nf+1/sigma^2) for each c */
		inv_v = 1.0/v;
		for(i=0;i<C;++i){
			d_delta_c[i] = (n+a)*f[i]*f[i]/((n+a)*f[i] + inv_v);
		}
		/* Compute the full sum of the denominator in Delta_delta and the second tern in the denominator*/
		d_delta_den1 = 1.0;
		for(i=0;i<C;++i){
			d_delta_den1 -= d_delta_c[i];
			d_delta_den2[i] = (n+a)*f[i] + inv_v;
		}
		/* compute the different terms in the numerator : remove the \tilde{c} terms */
		for(i=0;i<C;i++){
			d_delta_num[i] = d_delta_den1 + d_delta_c[i];
		}
		/* Finally compute Delta_delta */
		for(i=0;i<C;++i){
			d_delta_v[k][i] = d_delta_num[i]/(d_delta_den1*d_delta_den2[i]);
		}
	}// end v bins loop
	
	
	// get normalized likelihood from loglikelihood
	double sum_L;
	sum_L = 0.0;
	for(k=0;k<numbin;k++){
		lik[k] -= Lmax;
		lik[k] = exp(lik[k]);
		sum_L += lik[k];
	}
	for(k=0;k<numbin;k++){
		lik[k] /= sum_L;
	}
	
	/*
	// Multiply by prior
	for(k=0;k<numbin;k++){
		v = vmin * exp(deltav*k);
		lik[k] *= v*exp(-v);
	}
	// Renormalize
	sum_L = 0;
	for(k=0;k<numbin;k++){
		sum_L += lik[k];
	}
	for(k=0;k<numbin;k++){
		lik[k] /= sum_L;
	}
	*/
	
	// Average delta, and mu
	double w;
	mu = 0.0;
	for(k=0;k<numbin;k++){
		mu += lik[k]*mu_v[k];
	}

	// Compute var_delta = < (mu - <mu>)^2 > + <d_mu>
	var_mu = Psi_1((double) n);
	for(k=0;k<numbin;k++){
		var_mu += lik[k]*(mu_v[k] - mu)*(mu_v[k] - mu);
	}

	double *d_delta = new double [C];
	for(i=0;i<C;i++){
		delta[i] = 0.0;
		d_delta[i] = 0.0;
		for(k=0;k<numbin;k++){
			delta[i] += lik[k]*delta_v[k][i];
			d_delta[i] += lik[k]*d_delta_v[k][i];
		}
	}
		
	// Compute var_delta = < (delta - <delta>)^2 > + <d_delta>
	for(i=0;i<C;i++){
		var_delta[i] = 0.0;
		for(k=0;k<numbin;k++){
			var_delta[i] += lik[k]*(delta_v[k][i]-delta[i])*(delta_v[k][i]-delta[i]) + lik[k]*d_delta_v[k][i]*d_delta_v[k][i];
		}
	}

	delete[] f;
	for(k=0;k<numbin;++k){
        delete[] delta_v[k];
        delete[] d_delta_v[k];
    }
	delete[] delta_v;
	delete[] d_delta_v;
	delete[] d_delta;
	delete[] mu_v;
	return;
}

void parse_argv(int argc,char** argv, string &in_file, string &out_folder, int &N_threads, string &extended_output){

    if (argc<2)
        show_usage(argv[0]);

    int i;

    string get_help [2];
    get_help[0] = "-h";
    get_help[1] = "--help";

    for(i=1;i<argc;i++){
        if (argv[i] == get_help[0] || argv[i] == get_help[1])
            show_usage(argv[0]);
    }

	string get_version [2];
	get_version[0] = "-v";
    get_version[1] = "--version";
	for(i=1;i<argc;i++){
		if (argv[i] == get_version[0] || argv[i] == get_version[1]){
			cout << VERSION << "\n";
			exit(0);
		}
	}

    string to_find[4][2] = {{"-f", "--file"},
							{"-d", "--destination"},
							{"-n", "--n_threads"},
							{"-e", "--extended_output"}};

    int j;
    int idx;
    for(j=0;j<4;j++){
        idx = 0;
        for(i=1;i<argc;i++){
            if (argv[i] == to_find[j][0] || argv[i] == to_find[j][1]){
                idx = i;
                if ( idx+1 > argc-1 ){
                    cerr << "Error in argument parsing :\n"
                         << argv[i] << " option missing\n";
                    show_usage(argv[0]);
                }
                switch(j){
                    case 0: in_file = argv[idx+1];
                    case 1: out_folder = argv[idx+1];
                    case 2: N_threads = atoi(argv[idx+1]);
                    case 3: extended_output = argv[idx+1];
                }
				// remove last character of out_folder if it is '/'
				if( j == 1 && out_folder.back() == '/' )
					out_folder.pop_back();
            }
        }
        if (idx == 0 && j == 0){
        	cerr << "Error in argument parsing :\n"
            << "missing input file name\n";
            show_usage;	
        }
    }
}

static void show_usage(string name)
{
    cerr << "Usage: " << name << " <option(s)> SOURCES\n"
         << "Options:\n"
         << "\t-h,--help\t\tShow this help message\n"
         << "\t-v,--version\t\tShow the current version\n"
         << "\t-f,--file\t\tSpecify the input transcript count text file\n"
         << "\t-d,--destination\tSpecify the destination path (default: pwd)\n"
         << "\t-n,--n_threads\t\tSpecify the number of threads to be used (default: 4)\n"
         << "\t-e,--extended_output\tOption t print extended output (default: false)\n";
    exit(0);
}



