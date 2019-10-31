
/** Solver modeling
    Enable modeling of solution to improve performance
 **/

#include <stdlib.h>
#include <unistd.h>
#include "solver_ml.h"

#define CONFIGLOG "solver_ml.log"
#define CONFIGNAME "solver_ml.conf"
#define CONFIGPATH "/usr/local/share/gridlabd/"

// default configuration settings
double maximum_distance = 0; // 0 is never use solver model, 1e-9 is only when nearly identical
const char *solver_model_logfile = CONFIGLOG;
int solver_model_loglevel = -1; // -1=disable, 0 = minimal ... 9 = everything,
size_t maximum_models = 100; // maximum number of models to track
const char *model_dumpfile = NULL; // basename of dumpfile

SOLVERMODELSTATUS solver_model_status = SMS_INIT;
SOLVERMODEL *last = NULL, *first = NULL;
size_t solver_model_count = 0;
FILE *solver_model_logfh = NULL;
struct timeb start;

double solver_model_get_maximum_distance(void)
{
	return maximum_distance;
}

SOLVERMODELSTATUS solver_model_config(const char *localconfig = CONFIGNAME, 
									  const char *shareconfig = CONFIGPATH CONFIGNAME)
{
	SOLVERMODELSTATUS status = SMS_INIT;
	const char *configname = localconfig;
	FILE *fp = fopen(configname,"r");
	if ( fp == NULL )
	{
		configname = shareconfig;
		fp = fopen(configname,"r");
	}
	if ( fp != NULL )
	{
		char line[1024];
		while ( fgets(line,sizeof(line),fp) != NULL )
		{
			char tag[1024],value[1024];
			if ( sscanf(line,"%s%s",tag,value) == 2 )
			{
				if ( tag[0] == '#' )
				{
					continue;
				}
				else if ( strcmp(tag,"maximum_metric") == 0 )
				{
					maximum_distance = atof(value);
				}
				else if ( strcmp(tag,"logfile") == 0 )
				{
					solver_model_logfile = strdup(value);
				}
				else if ( strcmp(tag,"loglevel") == 0 )
				{
					solver_model_loglevel = atoi(value);
				}
				else if ( strcmp(tag,"method") == 0 )
				{
					if ( strcmp(value,"none") == 0 )
					{
						status = SMS_DISABLED;
					}
					else if ( strcmp(value,"basic") == 0 )
					{
						status = SMS_READY;
					}
					else 
					{
						fprintf(stdout,"solver_model_config(configname='%s'): method '%s' is not known, using basic method\n",configname,value);
					}
				}
				else if ( strcmp(tag,"dumpfile") == 0 )
				{
					model_dumpfile = strdup(value);
				}
				else
				{
					fprintf(stdout,"solver_model_config(configname='%s'): tag '%s' is not valid\n",configname,tag);
				}
			}
		}
		fclose(fp);
	}
	return status;
}

void solver_model_log(unsigned int level, const char *format, ...)
{
	if ( level <= solver_model_loglevel && solver_model_logfh != NULL )
	{
		va_list ptr;
		va_start(ptr,format);
		vfprintf(solver_model_logfh,format,ptr);
		fprintf(solver_model_logfh,"\n");
		va_end(ptr);
	}
}

void solver_model_start_timer(void)
{
	ftime(&start);
}

void solver_model_stop_timer(SOLVERMODEL *model, bool apply=false)
{
	struct timeb stop;
	ftime(&stop);
	double timeval = (stop.time-start.time) + (stop.millitm-start.millitm)/1000.0;
	if ( apply == false )
	{
		model->runtime = timeval;
		solver_model_log(1,"solver runtime: %.3f s",timeval);
	}
	else
	{
		solver_model_log(1,"model runtime: %.3f s (%.1f s saved)",timeval,model->runtime-timeval);
	}
}

int solver_model_init(void)
{
	solver_model_start_timer();
	switch ( solver_model_status )
	{
		case SMS_INIT:
			fprintf(stderr,"solver initialization started\n");
			solver_model_status = solver_model_config();
			if ( solver_model_status == SMS_READY )
			{
				if ( solver_model_logfh != NULL )
				{
					fclose(solver_model_logfh);
				}
				solver_model_logfh = NULL;
				if ( solver_model_loglevel > 0 )
				{
					solver_model_logfh = fopen(solver_model_logfile,"w");
					if ( solver_model_logfh )
					{
						fprintf(stderr,"solver log %s opened ok\n",solver_model_logfile);
					}
					else
					{
						fprintf(stderr,"solver log %s opened failed (errno=%d, strerror='%s')\n",solver_model_logfile,errno,strerror(errno));
					}
				}
				else
				{
					unlink(solver_model_logfile);
				}
				return 1;
			}
			else
			{
				fprintf(stderr,"solver configuration failed\n");
				return 0;
			}
		case SMS_READY:
			fprintf(stderr,"solver ready\n");
			return 1;
		case SMS_DISABLED:
			fprintf(stderr,"solver initialization disabled\n");
			return 0;
		case SMS_FAILED:
			fprintf(stderr,"solver initialization failed\n");
			return 0;
		default:
			fprintf(stderr,"solver state unknown (solver_model_status=%d)\n", solver_model_status);
			return 0;
	}
}

#define MAXDIST 9.999999e99
#define ALPHA 0.5 // parameter to tune how much more important bus metric is than branch metric
double solver_get_metric(SOLVERMODEL *model, // use NULL to get absolute metric for problem id
						 unsigned int &bus_count,
						 BUSDATA *&bus,
						 unsigned int &branch_count,
						 BRANCHDATA *&branch)
{
	if ( model && ( model->bus_count != bus_count || model->branch_count != branch_count) )
	{
		return MAXDIST;
	}
	double bus_dist = 0, branch_dist = 0;
	size_t n;
	unsigned char phases[3] = {0x01,0x02,0x04};
	for ( n = 0 ; n < bus_count ; n++ )
	{
		size_t phase;
		for ( phase = 0 ; phase < 3 ; phase++ )
		{
			if ( (bus[n].phases&phases[phase]) == 0 )
				continue;

			// load contribution to power distance
			complex P = complex(bus[n].PL[phase]);
			if ( model )
			{
				P -= complex(model->bus[n].PL[phase],model->bus[n].QL[phase]);
			}
			double dist = P.Mag();
			if ( dist > 0 && model )
				solver_model_log(2,"bus %d (%s) phase %c distance %g", n, bus[n].name, 'A'+phase, dist);
			bus_dist += dist; 
			
			// TODO: generation contribution to power distance
		}
	}
	for ( n = 0 ; n < branch_count ; n ++ )
	{
		size_t phase;
		for ( phase = 0; phase < 3 ; phase++ )
		{
			// line loss contribution to power distance
			int in = branch[n].from;
			int out = branch[n].to;

			if ( (bus[in].phases&phases[phase]) == 0 
				|| (bus[out].phases&phases[phase]) == 0 )
				continue;

			complex Vin = bus[in].V[phase];
			complex Vout = bus[out].V[phase];
			complex Yin = branch[n].Yfrom[phase];
			complex Yout = branch[n].Yto[phase];
			complex Pin = Vin*~(Vin*Yin);
			complex Pout = Vout*~(Vout*Yout);
			complex Pdiff = (Pin - Pout);
			if ( model )
			{
				Vin = model->bus[in].V[phase];
				Vout = model->bus[out].V[phase];
				Yin = model->branch[n].Yfrom[phase];
				Yout = model->branch[n].Yto[phase];
				Pin = Vin*~(Vin*Yin);
				Pout = Vout*~(Vout*Yout);
				Pdiff -= (Pin - Pout);
			}
			double dist = Pdiff.Mag();
			if ( dist > 0 && model )
				solver_model_log(2,"branch %d (%s) phase %c distance %g", n, branch[n].name, 'A'+phase, dist);
			branch_dist += dist;
		}
		// TODO: calculate self-impedance distance
	}
	double metric = ALPHA*sqrt(bus_dist)/bus_count + (1.0-ALPHA)*sqrt(branch_dist)/branch_count;
	if ( model )
	{
		solver_model_log(1,"model %llx (%dx%d) distance is %g",(model?model->id:0),bus_count,branch_count,metric);
	}
	else
	{
		solver_model_log(1,"new model (%dx%d) distance is %g",bus_count,branch_count,metric);
	}
	return metric;
}

PROBLEMID solver_problem_id(unsigned int bus_count,
							BUSDATA *bus, 
							unsigned int branch_count, 
							BRANCHDATA *branch)
{
	double metric = solver_get_metric(NULL,bus_count,bus,branch_count,branch);
	return (PROBLEMID)(metric*1000.0);
}


void *memdup(const void *mem, size_t bytes)
{
	if ( mem == NULL || bytes == 0 )
	{
		return NULL;
	}
	void *ptr = (void*) new char[bytes];
	return ptr ? memcpy(ptr,mem,bytes) : NULL;
}

void solver_model_del(SOLVERMODEL *model)
{
	solver_model_log(1,"deleted model %x (%dx%d)", model->id, model->bus_count, model->branch_count);
	if ( first == model )
	{
		first = model->prev;
	}
	if ( last == model )
	{
		last = model->next;
	}
	if ( model->prev != NULL )
	{
		model->prev->next = model->next;
	}
	if ( model->next != NULL )
	{
		model->next->prev = model->prev;
	}
	delete [] model->bus;
	delete [] model->branch;
	delete [] model->mesh_imped_values;
	if ( model->bad_computations != NULL ) 
	{
		delete [] model->bad_computations;
	}
	delete model;
	solver_model_count--;
}

SOLVERMODEL *solver_model_new(unsigned int bus_count,
							  BUSDATA *bus, 
							  unsigned int branch_count, 
							  BRANCHDATA *branch, 
							  NR_SOLVER_STRUCT *powerflow_values, 
							  NRSOLVERMODE powerflow_type , 
							  NR_MESHFAULT_IMPEDANCE *mesh_imped_values, 
							  bool *bad_computations,
							  int64 iterations)
{
	SOLVERMODEL *model = new SOLVERMODEL;
	if ( ! model )
	{
		solver_model_log(0,"memory allocation failed");
		return NULL;
	}
	model->id = solver_problem_id(bus_count,bus,branch_count,branch);
	model->bus_count = bus_count;
	model->bus = (BUSDATA*)memdup((void*)bus,sizeof(BUSDATA)*bus_count);
	// TODO: check to see if any contents also should be duplicated (e.g., links)
	model->branch_count = branch_count;
	model->branch = (BRANCHDATA*)memdup((void*)branch,sizeof(BRANCHDATA)*branch_count);
	// TODO: check to see if any contents also should be duplicated (e.g., Y values)
	model->powerflow_values = (NR_SOLVER_STRUCT*)memdup((void*)powerflow_values,sizeof(NR_SOLVER_STRUCT));
	model->powerflow_type = powerflow_type;
	model->mesh_imped_values = (NR_MESHFAULT_IMPEDANCE*)memdup((void*)mesh_imped_values,sizeof(NR_MESHFAULT_IMPEDANCE));
	model->bad_computations = ( bad_computations ? NULL : (bool*)memdup((void*)bad_computations,sizeof(bool)) );
	model->iterations = iterations;
	model->next = last;
	model->prev = NULL;
	solver_model_count++;
	if ( first == NULL )
	{
		first = model;
	}
	else if ( solver_model_count > maximum_models )
	{
		solver_model_del(first);
	}
	if ( last != NULL )
	{
		last->prev = model;
	}
	last = model;
	solver_model_log(1,"adding model %x (%dx%d)", model->id, bus_count, branch_count);
	solver_model_stop_timer(model);
	return last;
}

// iterators
SOLVERMODEL *solver_model_getfirst() 
{
	return last; // LIFO
}
SOLVERMODEL *solver_model_getnext(SOLVERMODEL *model)
{
	return model->next;
}
bool solver_model_islast(SOLVERMODEL *model)
{
	return model ? model->next == NULL : true;
}

void solver_model_dump(SOLVERMODEL *m)
{

}

// solver_find_model(SOLVERMODEL): identifies a satisfactory model and return the distance metric
bool solver_model_find(SOLVERMODEL *&model,
						 unsigned int &bus_count,
						 BUSDATA *&bus,
						 unsigned int &branch_count,
						 BRANCHDATA *&branch)
{
	double dist = MAXDIST;
	SOLVERMODEL *m;
	for ( m = solver_model_getfirst() ; m != NULL ; m = solver_model_getnext(m) )
	{
		double n = solver_get_metric(m,bus_count,bus,branch_count,branch);
		solver_model_log(1,"check model %x (%dx%d) old distance %g", m->id, bus_count, branch_count, dist);
		if ( n < dist )
		{
			model = m;
			dist = n;
		}
	}
	if ( model != NULL )
	{
		solver_model_log(1,"found model %x (%dx%d) new distance %g", model->id, bus_count, branch_count, dist);
		return dist < maximum_distance; 
	}
	else
	{
		return false;
	}
}

int64 solver_model_apply(SOLVERMODEL *model,
						NR_SOLVER_STRUCT *&powerflow_values,
						NRSOLVERMODE &powerflow_type,
						NR_MESHFAULT_IMPEDANCE *&mesh_imped_values,
						bool *&bad_computations,
						int64 iterations)
{
	powerflow_values = model->powerflow_values;
	powerflow_type = model->powerflow_type;
	mesh_imped_values = model->mesh_imped_values;
	bad_computations = model->bad_computations;
	solver_model_stop_timer(model,true);
	return model->iterations; 
}
