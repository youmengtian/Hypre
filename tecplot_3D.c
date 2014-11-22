#include <stdio.h>
#include <stdlib.h>

int main()
{
	FILE *f_input;
	char *i_filename;
	double result;
	FILE *f_output;
	char *o_filename;
	int n,N;
	double x,y,z,h;
	int i;
	
	n=55;
	
	/*��ȡ����ļ���ע������ļ�Ӧ�÷���.c�ļ�ͬһ���ļ���*/
	i_filename = "result.txt";	//�Ƚ��и�ֵ֮��֮����Ҫ��argv����
	if ((f_input = fopen(i_filename,"r")) == NULL)
	{
		printf("Error: can't open solution result file %s\n", i_filename);
		exit(1);
	}

	
	/*��������ļ�������ļ�����Ҫ��tecplot��ʽ��dat�ļ�*/

	o_filename = "tec_result.dat";
	if ((f_output = fopen(o_filename,"w")) == NULL)
	{
		printf("Error: can't open solution result file %s\n", o_filename);
		exit(1);
	}

	/*�ļ�ͷ*/
	fprintf(f_output,"%s\n","TITLE = \"Hypre Solution File\"");
	fprintf(f_output,"%s\n","FILETYPE = FULL");
	fprintf(f_output,"%s\n","VARIABLES = \"X\" \"Y\" \"Z\" \"U\"");
	fprintf(f_output,"%s %s%d %s%d %s%d %s\n","ZONE","I=",n,"J=",n,"K=",n,"DATAPACKING=POINT");
	N = n * n * n;
	h = 1.0/(n-1);

	/*д��X*/
	for(i = 0; i < N; i++)
	{
		z = i/(n*n);
		y = (i - z*n*n)/n;
		x = i % n;
		fscanf(f_input,"%lf",&result);
		fprintf(f_output,"%lf %lf %lf %lf\n",x*h,y*h,z*h,result);
		//fprintf(f_output,"%s"," ");
	}
	fprintf(f_output,"%s","\n");

	///*д��Y*/
	//for(i = 0; i < N; i++)
	//{
	//	fprintf(f_output,"%lf",((i/n) * h));
	//	fprintf(f_output,"%s"," ");
	//}
	//fprintf(f_output,"%s","\n");

	///*д��U*/
	//for(i = 0; i < N; i++)
	//{
	//	fscanf(f_input,"%lf",&result);
	//	fprintf(f_output,"%lf",result);
	//	fprintf(f_output,"%s"," ");
	//}
	//fprintf(f_output,"%s","\n");

	//fscanf(f_input,"%lf",&result);
	//printf("%lf\n",result);
	//fscanf(f_input,"%lf",&result);
	//printf("%lf\n",result);
	fclose(f_input);
	fclose(f_output);
	return 0;
}
	
	