/*******************************************************************************************************/
//
// 实现以下功能：
// 1、可以对各种方法的运行时间进行统计；
// 2、计算3-D Laplacian方程，计算网格为n x n x n，未知数个数 N = n^3
// 3、对solver进行扩展以实现更多的算法：AMG、AMG-PCG、AMG-BiCGSTAB、AMG-FlexGMRES、ParaSails-PCG、PCG；
// 4、以Tecplot结构化网格形式输出计算结果。
//
/*******************************************************************************************************/


#include <math.h>
#include <time.h>
#include "_hypre_utilities.h"
#include "HYPRE_krylov.h"
#include "HYPRE.h"
#include "HYPRE_parcsr_ls.h"

int hypre_FlexGMRESModifyPCAMGExample(void *precond_data, int iterations,
                                      double rel_residual_norm);


int main (int argc, char *argv[])
{
   int i;
   int myid, num_procs;
   int N, n;
   int ilower, iupper;
   int local_size, extra;

   int solver_id;
   int print_system,print_init;

   double h, h2;

   HYPRE_IJMatrix A;
   HYPRE_ParCSRMatrix parcsr_A;
   HYPRE_IJVector b;
   HYPRE_ParVector par_b;
   HYPRE_IJVector x;
   HYPRE_ParVector par_x;

   HYPRE_Solver solver, precond;

   /* Initialize MPI */
   MPI_Init(&argc, &argv);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

   /* Default problem parameters */
   n = 33;
   solver_id = 0;
   print_system = 0;
   print_init = 0;


   /* Parse command line */
   {
      int arg_index = 0;
      int print_usage = 0;

      while (arg_index < argc)
      {
         if ( strcmp(argv[arg_index], "-n") == 0 )
         {
            arg_index++;
            n = atoi(argv[arg_index++]);
         }
         else if ( strcmp(argv[arg_index], "-solver") == 0 )
         {
            arg_index++;
            solver_id = atoi(argv[arg_index++]);
         }
		 else if ( strcmp(argv[arg_index], "-print_init") == 0 )
         {
            arg_index++;
            print_init = 1;
         }

         else if ( strcmp(argv[arg_index], "-print_system") == 0 )
         {
            arg_index++;
            print_system = 1;
         }
         else if ( strcmp(argv[arg_index], "-help") == 0 )
         {
            print_usage = 1;
            break;
         }
         else
         {
            arg_index++;
         }
      }

      if ((print_usage) && (myid == 0))
      {
         printf("\n");
         printf("Usage: %s [<options>]\n", argv[0]);
         printf("\n");
         printf("  -n <n>              : problem size in each direction (default: 33)\n");
         printf("  -solver <ID>        : solver ID\n");
         printf("                        0  - AMG (default) \n");
         printf("                        1  - AMG-PCG\n");
		 printf("                        2  - AMG-BiCGSTAB\n");
         printf("                        3  - ParaSails-PCG\n");
         printf("                        4 - PCG\n");
         printf("                        5 - AMG-FlexGMRES\n");
         printf("  -print_init       : print the matrix and rhs\n");
		 printf("  -print_system     : print the solution x\n");
         printf("\n");
      }

      if (print_usage)
      {
         MPI_Finalize();
         return (0);
      }
   }

   /* Preliminaries: want at least one processor per row */
   if (n*n < num_procs) n = sqrt(num_procs) + 1;
   N = n*n*n; /* global number of rows */
   h = 1.0/(n+1); /* mesh size*/
   h2 = h*h;

   /* Each processor knows only of its own rows - the range is denoted by ilower
      and upper.  Here we partition the rows. We account for the fact that
      N may not divide evenly by the number of processors. */
   local_size = N/num_procs;
   extra = N - local_size*num_procs;

   ilower = local_size*myid;
   ilower += hypre_min(myid, extra);

   iupper = local_size*(myid+1);
   iupper += hypre_min(myid+1, extra);
   iupper = iupper - 1;

   /* How many rows do I have? */
   local_size = iupper - ilower + 1;

   /* Create the matrix.
      Note that this is a square matrix, so we indicate the row partition
      size twice (since number of rows = number of cols) */
   HYPRE_IJMatrixCreate(MPI_COMM_WORLD, ilower, iupper, ilower, iupper, &A);

   /* Choose a parallel csr format storage (see the User's Manual) */
   HYPRE_IJMatrixSetObjectType(A, HYPRE_PARCSR);

   /* Initialize before setting coefficients */
   HYPRE_IJMatrixInitialize(A);

   /* Now go through my local rows and set the matrix entries.
      Each row has at most 5 entries. For example, if n=3:

      A = [M -I 0; -I M -I; 0 -I M]
      M = [4 -1 0; -1 4 -1; 0 -1 4]

      Note that here we are setting one row at a time, though
      one could set all the rows together (see the User's Manual).
   */
   {
      int nnz;
      double values[7];
      int cols[7];

      for (i = ilower; i <= iupper; i++)
      {
         nnz = 0;
		 /* The lower identity block:position i -n*n */
		 if((i-n*n) >= 0)
		 {
			 cols[nnz] = i-n*n;
			 values[nnz] = -1.0;
			 nnz++;
		 }

         /* The left identity block:position i-n */
         if ((i%(n*n)-n)>=0)
         {
            cols[nnz] = i-n;
            values[nnz] = -1.0;
            nnz++;
         }

         /* The left -1: position i-1 */
         if (i%n)
         {
            cols[nnz] = i-1;
            values[nnz] = -1.0;
            nnz++;
         }

         /* Set the diagonal: position i */
         cols[nnz] = i;
         values[nnz] = 6.0;
         nnz++;

         /* The right -1: position i+1 */
         if ((i%n) < (n-1))
         {
            cols[nnz] = i+1;
            values[nnz] = -1.0;
            nnz++;
         }

         /* The right identity block:position i+n */
         if ((i%(n*n))<(n*n-n))
         {
            cols[nnz] = i+n;
            values[nnz] = -1.0;
            nnz++;
         }

		 /* The upper identity block:position i+n*n */
         if ((i+n*n)< N)
         {
            cols[nnz] = i+n*n;
            values[nnz] = -1.0;
            nnz++;
         }

         /* Set the values for row i */
         HYPRE_IJMatrixSetValues(A, 1, &nnz, &i, cols, values);
      }
   }

   /* Assemble after setting the coefficients */
   HYPRE_IJMatrixAssemble(A);

   /* Note: for the testing of small problems, one may wish to read
      in a matrix in IJ format (for the format, see the output files
      from the -print_system option).
      In this case, one would use the following routine:
      HYPRE_IJMatrixRead( <filename>, MPI_COMM_WORLD,
                          HYPRE_PARCSR, &A );
      <filename>  = IJ.A.out to read in what has been printed out
      by -print_system (processor numbers are omitted).
      A call to HYPRE_IJMatrixRead is an *alternative* to the
      following sequence of HYPRE_IJMatrix calls:
      Create, SetObjectType, Initialize, SetValues, and Assemble
   */


   /* Get the parcsr matrix object to use */
   HYPRE_IJMatrixGetObject(A, (void**) &parcsr_A);


   /* Create the rhs and solution */
   HYPRE_IJVectorCreate(MPI_COMM_WORLD, ilower, iupper,&b);
   HYPRE_IJVectorSetObjectType(b, HYPRE_PARCSR);
   HYPRE_IJVectorInitialize(b);

   HYPRE_IJVectorCreate(MPI_COMM_WORLD, ilower, iupper,&x);
   HYPRE_IJVectorSetObjectType(x, HYPRE_PARCSR);
   HYPRE_IJVectorInitialize(x);

   /* Set the rhs values to h^2 and the solution to zero */
   {
      double *rhs_values, *x_values;
      int    *rows;

      rhs_values = calloc(local_size, sizeof(double));
      x_values = calloc(local_size, sizeof(double));
      rows = calloc(local_size, sizeof(int));

      for (i=0; i<local_size; i++)
      {
		 int I,J,K;
		 K = i/(n*n);
		 J = (i-K*n*n)/n;
		 I = i-K*n*n-J*n;
         x_values[i] = 0.0;
		 //rhs_values[i]
		 rhs_values[i] = 0;


		 /*lower*/
		 if(K == 0)
		 {
			 /*left and right line*/
			 if(J==0||J == (n-1))
			 {
				 if(I==0||I==(n-1))
					 rhs_values[i] = 300;
				 else
					 rhs_values[i] = 200;
			 }
			
			 /*the other*/
			 else
			 {
				 if(I==0||I==(n-1))// other line
					 rhs_values[i] = 200;
				 else
					 rhs_values[i] = 100;
			 }
		 }


		 /*upper*/
		 if(K == (n-1))
		 {
			 /*left and right line*/
			 if(J==0||J == (n-1))
			 {
				 if(I==0||I==(n-1))
					 rhs_values[i] = 30;
				 else
					 rhs_values[i] = 20;
			 }
			
			 /*the other*/
			 else
			 {
				 if(I==0||I==(n-1))// other line
					 rhs_values[i] = 20;
				 else
					 rhs_values[i] = 10;
			 }
		 }


		 /*left and right*/
		 if((J == 0||J == (n-1))&&K!=0&&K!=(n-1))
		 {
			 if(I == 0||I == (n-1))
				 rhs_values[i] = 20;
			 else
				 rhs_values[i] = 10;
		 }


		 /*front and back*/
		 if((I == 0||I == (n-1))&&K!=0&&K!=(n-1)&&J!=0&&J!=(n-1))
			 rhs_values[i] = 10;


         rows[i] = ilower + i;
      }


      HYPRE_IJVectorSetValues(b, local_size, rows, rhs_values);
      HYPRE_IJVectorSetValues(x, local_size, rows, x_values);

      free(x_values);
      free(rhs_values);
      free(rows);
   }


   HYPRE_IJVectorAssemble(b);
   /*  As with the matrix, for testing purposes, one may wish to read in a rhs:
       HYPRE_IJVectorRead( <filename>, MPI_COMM_WORLD,
                                 HYPRE_PARCSR, &b );
       as an alternative to the
       following sequence of HYPRE_IJVectors calls:
       Create, SetObjectType, Initialize, SetValues, and Assemble
   */
   HYPRE_IJVectorGetObject(b, (void **) &par_b);

   HYPRE_IJVectorAssemble(x);
   HYPRE_IJVectorGetObject(x, (void **) &par_x);


if(print_init)
{
   HYPRE_IJMatrixPrint(A, "IJ.out.A");
   HYPRE_IJVectorPrint(b, "IJ.out.b");
}
   /* Choose a solver and solve the system */

   /* AMG */
   if (solver_id == 0)
   {
      int num_iterations;
      double final_res_norm;
	  clock_t start,finish;
	  double time_used;

	  start  = clock();

      /* Create solver */
      HYPRE_BoomerAMGCreate(&solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_BoomerAMGSetPrintLevel(solver, 3);  /* print solve info + parameters */
      HYPRE_BoomerAMGSetCoarsenType(solver, 6); /* Falgout coarsening */
      HYPRE_BoomerAMGSetRelaxType(solver, 3);   /* G-S/Jacobi hybrid relaxation */
      HYPRE_BoomerAMGSetNumSweeps(solver, 1);   /* Sweeeps on each level */
      HYPRE_BoomerAMGSetMaxLevels(solver, 20);  /* maximum number of levels */
      HYPRE_BoomerAMGSetTol(solver, 1e-7);      /* conv. tolerance */

      /* Now setup and solve! */
      HYPRE_BoomerAMGSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_BoomerAMGSolve(solver, parcsr_A, par_b, par_x);

      /* Run info - needed logging turned on */
      HYPRE_BoomerAMGGetNumIterations(solver, &num_iterations);
      HYPRE_BoomerAMGGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destroy solver */
      HYPRE_BoomerAMGDestroy(solver);
	  finish = clock();
	  time_used = (finish - start)/1000.0;
	  printf("Time used by AMG:%.6f\n",time_used);
   }
   /* PCG */
   else if (solver_id == 4)
   {
      int num_iterations;
      double final_res_norm;
	  clock_t start,finish;
	  double time_used;

	  start = clock();
      /* Create solver */
      HYPRE_ParCSRPCGCreate(MPI_COMM_WORLD, &solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_PCGSetMaxIter(solver, 1000); /* max iterations */
      HYPRE_PCGSetTol(solver, 1e-7); /* conv. tolerance */
      HYPRE_PCGSetTwoNorm(solver, 1); /* use the two norm as the stopping criteria */
      HYPRE_PCGSetPrintLevel(solver, 2); /* prints out the iteration info */
      HYPRE_PCGSetLogging(solver, 1); /* needed to get run info later */

      /* Now setup and solve! */
      HYPRE_ParCSRPCGSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_ParCSRPCGSolve(solver, parcsr_A, par_b, par_x);

      /* Run info - needed logging turned on */
      HYPRE_PCGGetNumIterations(solver, &num_iterations);
      HYPRE_PCGGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destroy solver */
      HYPRE_ParCSRPCGDestroy(solver);
	  finish = clock();
	  time_used = (finish - start)/1000.;
	  printf("Time used by PCG:%.6f\n",time_used);
   }
   /* PCG with AMG preconditioner */
   else if (solver_id == 1)
   {
      int num_iterations;
      double final_res_norm;
	  clock_t start,finish;
	  double time_used;

	  start = clock();
      /* Create solver */
      HYPRE_ParCSRPCGCreate(MPI_COMM_WORLD, &solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_PCGSetMaxIter(solver, 1000); /* max iterations */
      HYPRE_PCGSetTol(solver, 1e-7); /* conv. tolerance */
      HYPRE_PCGSetTwoNorm(solver, 1); /* use the two norm as the stopping criteria */
      HYPRE_PCGSetPrintLevel(solver, 2); /* print solve info */
      HYPRE_PCGSetLogging(solver, 1); /* needed to get run info later */

      /* Now set up the AMG preconditioner and specify any parameters */
      HYPRE_BoomerAMGCreate(&precond);
      HYPRE_BoomerAMGSetPrintLevel(precond, 1); /* print amg solution info */
      HYPRE_BoomerAMGSetCoarsenType(precond, 6);
      HYPRE_BoomerAMGSetRelaxType(precond, 6); /* Sym G.S./Jacobi hybrid */
      HYPRE_BoomerAMGSetNumSweeps(precond, 1);
      HYPRE_BoomerAMGSetTol(precond, 0.0); /* conv. tolerance zero */
      HYPRE_BoomerAMGSetMaxIter(precond, 1); /* do only one iteration! */

      /* Set the PCG preconditioner */
      HYPRE_PCGSetPrecond(solver, (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                          (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup, precond);

      /* Now setup and solve! */
      HYPRE_ParCSRPCGSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_ParCSRPCGSolve(solver, parcsr_A, par_b, par_x);

      /* Run info - needed logging turned on */
      HYPRE_PCGGetNumIterations(solver, &num_iterations);
      HYPRE_PCGGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destroy solver and preconditioner */
      HYPRE_ParCSRPCGDestroy(solver);
      HYPRE_BoomerAMGDestroy(precond);
	  finish = clock();
	  time_used = (finish - start)/1000.;
	  printf("Time used by AMG-PCG:%.6f\n",time_used);
   }

   /* BiCGSTAB with AMG preconditioner */
   else if(solver_id == 2)
   {
	   int num_iterations;
      double final_res_norm;
	  clock_t start,finish;
	  double time_used;

	  start = clock();
      /* Create solver */
      HYPRE_ParCSRBiCGSTABCreate(MPI_COMM_WORLD, &solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_BiCGSTABSetMaxIter(solver, 1000); /* max iterations */
      HYPRE_BiCGSTABSetTol(solver, 1e-7); /* conv. tolerance */
      HYPRE_BiCGSTABSetPrintLevel(solver, 2); /* print solve info */
      HYPRE_BiCGSTABSetLogging(solver, 1); /* needed to get run info later */

      /* Now set up the AMG preconditioner and specify any parameters */
      HYPRE_BoomerAMGCreate(&precond);
      HYPRE_BoomerAMGSetPrintLevel(precond, 1); /* print amg solution info */
      HYPRE_BoomerAMGSetCoarsenType(precond, 6);
      HYPRE_BoomerAMGSetRelaxType(precond, 6); /* Sym G.S./Jacobi hybrid */
      HYPRE_BoomerAMGSetNumSweeps(precond, 1);
      HYPRE_BoomerAMGSetTol(precond, 0.0); /* conv. tolerance zero */
      HYPRE_BoomerAMGSetMaxIter(precond, 1); /* do only one iteration! */

      /* Set the PCG preconditioner */
      HYPRE_BiCGSTABSetPrecond(solver, (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                          (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup, precond);

      /* Now setup and solve! */
      HYPRE_ParCSRBiCGSTABSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_ParCSRBiCGSTABSolve(solver, parcsr_A, par_b, par_x);

      /* Run info - needed logging turned on */
      HYPRE_BiCGSTABGetNumIterations(solver, &num_iterations);
      HYPRE_BiCGSTABGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destroy solver and preconditioner */
      HYPRE_ParCSRBiCGSTABDestroy(solver);
      HYPRE_BoomerAMGDestroy(precond);
	  finish = clock();
	  time_used = (finish - start)/1000.;
	  printf("Time used by AMG-BiCGSTAB:%.6f\n",time_used);
   }

   /* PCG with Parasails Preconditioner */
   else if (solver_id == 3)
   {
      int    num_iterations;
      double final_res_norm;

      int      sai_max_levels = 1;
      double   sai_threshold = 0.1;
      double   sai_filter = 0.05;
      int      sai_sym = 1;
	  clock_t start,finish;
	  double time_used;

	  start = clock();
      /* Create solver */
      HYPRE_ParCSRPCGCreate(MPI_COMM_WORLD, &solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_PCGSetMaxIter(solver, 1000); /* max iterations */
      HYPRE_PCGSetTol(solver, 1e-7); /* conv. tolerance */
      HYPRE_PCGSetTwoNorm(solver, 1); /* use the two norm as the stopping criteria */
      HYPRE_PCGSetPrintLevel(solver, 2); /* print solve info */
      HYPRE_PCGSetLogging(solver, 1); /* needed to get run info later */

      /* Now set up the ParaSails preconditioner and specify any parameters */
      HYPRE_ParaSailsCreate(MPI_COMM_WORLD, &precond);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_ParaSailsSetParams(precond, sai_threshold, sai_max_levels);
      HYPRE_ParaSailsSetFilter(precond, sai_filter);
      HYPRE_ParaSailsSetSym(precond, sai_sym);
      HYPRE_ParaSailsSetLogging(precond, 3);

      /* Set the PCG preconditioner */
      HYPRE_PCGSetPrecond(solver, (HYPRE_PtrToSolverFcn) HYPRE_ParaSailsSolve,
                          (HYPRE_PtrToSolverFcn) HYPRE_ParaSailsSetup, precond);

      /* Now setup and solve! */
      HYPRE_ParCSRPCGSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_ParCSRPCGSolve(solver, parcsr_A, par_b, par_x);


      /* Run info - needed logging turned on */
      HYPRE_PCGGetNumIterations(solver, &num_iterations);
      HYPRE_PCGGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destory solver and preconditioner */
      HYPRE_ParCSRPCGDestroy(solver);
      HYPRE_ParaSailsDestroy(precond);
	  finish = clock();
	  time_used = (finish - start)/1000.;
	  printf("Time used by ParaSails-PCG:%.6f\n",time_used);
   }
   /* Flexible GMRES with  AMG Preconditioner */
   else if (solver_id == 5)
   {
      int    num_iterations;
      double final_res_norm;
      int    restart = 30;
      int    modify = 1;
	  clock_t start,finish;
	  double time_used;

	  start = clock();
      /* Create solver */
      HYPRE_ParCSRFlexGMRESCreate(MPI_COMM_WORLD, &solver);

      /* Set some parameters (See Reference Manual for more parameters) */
      HYPRE_FlexGMRESSetKDim(solver, restart);
      HYPRE_FlexGMRESSetMaxIter(solver, 1000); /* max iterations */
      HYPRE_FlexGMRESSetTol(solver, 1e-7); /* conv. tolerance */
      HYPRE_FlexGMRESSetPrintLevel(solver, 2); /* print solve info */
      HYPRE_FlexGMRESSetLogging(solver, 1); /* needed to get run info later */


      /* Now set up the AMG preconditioner and specify any parameters */
      HYPRE_BoomerAMGCreate(&precond);
      HYPRE_BoomerAMGSetPrintLevel(precond, 1); /* print amg solution info */
      HYPRE_BoomerAMGSetCoarsenType(precond, 6);
      HYPRE_BoomerAMGSetRelaxType(precond, 6); /* Sym G.S./Jacobi hybrid */
      HYPRE_BoomerAMGSetNumSweeps(precond, 1);
      HYPRE_BoomerAMGSetTol(precond, 0.0); /* conv. tolerance zero */
      HYPRE_BoomerAMGSetMaxIter(precond, 1); /* do only one iteration! */

      /* Set the FlexGMRES preconditioner */
      HYPRE_FlexGMRESSetPrecond(solver, (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                          (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup, precond);


      if (modify)
      /* this is an optional call  - if you don't call it, hypre_FlexGMRESModifyPCDefault
         is used - which does nothing.  Otherwise, you can define your own, similar to
         the one used here */
         HYPRE_FlexGMRESSetModifyPC( solver,
                                     (HYPRE_PtrToModifyPCFcn) hypre_FlexGMRESModifyPCAMGExample);


      /* Now setup and solve! */
      HYPRE_ParCSRFlexGMRESSetup(solver, parcsr_A, par_b, par_x);
      HYPRE_ParCSRFlexGMRESSolve(solver, parcsr_A, par_b, par_x);

      /* Run info - needed logging turned on */
      HYPRE_FlexGMRESGetNumIterations(solver, &num_iterations);
      HYPRE_FlexGMRESGetFinalRelativeResidualNorm(solver, &final_res_norm);
      if (myid == 0)
      {
         printf("\n");
         printf("Iterations = %d\n", num_iterations);
         printf("Final Relative Residual Norm = %e\n", final_res_norm);
         printf("\n");
      }

      /* Destory solver and preconditioner */
      HYPRE_ParCSRFlexGMRESDestroy(solver);
      HYPRE_BoomerAMGDestroy(precond);
	  finish = clock();
	  time_used = (finish - start)/1000.;
	  printf("Time used by AMG-FlexGMRES:%.6f\n",time_used);

   }
   else
   {
      if (myid ==0) printf("Invalid solver id specified.\n");
   }


   if (print_system)
   {
	  FILE *file;
      char filename[255];

      int nvalues = local_size;
      int *rows = calloc(nvalues, sizeof(int));
      double *values = calloc(nvalues, sizeof(double));

      for (i = 0; i < nvalues; i++)
         rows[i] = ilower + i;

      /* get the local solution */
      HYPRE_IJVectorGetValues(x, nvalues, rows, values);

      sprintf(filename, "%s_%d.txt", "solution", n);
      if ((file = fopen(filename, "w")) == NULL)
      {
         printf("Error: can't open output file %s\n", filename);
         MPI_Finalize();
         exit(1);
      }

      /* save solution */
      for (i = 0; i < nvalues; i++)
         fprintf(file, "%.14e\n", values[i]);

      fflush(file);
      fclose(file);

      free(rows);
      free(values);

	  
   }

   /* Clean up */
   HYPRE_IJMatrixDestroy(A);
   HYPRE_IJVectorDestroy(b);
   HYPRE_IJVectorDestroy(x);

   /* Finalize MPI*/
   MPI_Finalize();

   return(0);
}

/*--------------------------------------------------------------------------
   hypre_FlexGMRESModifyPCAMGExample -

    This is an example (not recommended)
   of how we can modify things about AMG that
   affect the solve phase based on how FlexGMRES is doing...For
   another preconditioner it may make sense to modify the tolerance..

 *--------------------------------------------------------------------------*/

int hypre_FlexGMRESModifyPCAMGExample(void *precond_data, int iterations,
                                   double rel_residual_norm)
{


   if (rel_residual_norm > .1)
   {
      HYPRE_BoomerAMGSetNumSweeps(precond_data, 10);
   }
   else
   {
      HYPRE_BoomerAMGSetNumSweeps(precond_data, 1);
   }


   return 0;
}