#include <stdlib.h>
#include <string.h>
#include <complex.h>

#include <errlog.h>

#include <menuFtype.h>
#include <aSubRecord.h>
#include <recGbl.h>
#include <alarm.h>
#include <dbAccess.h>

#include <registryFunction.h>
#include <postfix.h>

#include <epicsMath.h>
#include <epicsTypes.h>

#undef I /* avoid conflict between complex.h and variables in this file */

#define PI 3.14159265359

#define MIN(A,B) ((A)<(B) ? (A) : (B))
#define MAX(A,B) ((A)>(B) ? (A) : (B))

/* wrap arbitrary phase to [-180, 180) */
static
double phase_wrap(double pha)
{
    double out = fmod(pha, 360.0);
    // (-360, -360) -> [-180, 180)
    if(out < -180.0)
        out += 360.0;
    else if(out >= 180.0)
        out -= 360.0;
    return out;
}

/* Element wise waveform calculator
 *
 * record(stringout, "$(N):expr") {
 *  field(VAL, "C*sin(2*A+B)")
 *  field(FLNK, "$(N)")
 * }
 * record(aSub, "$(N)") {
 *  field(INAM, "WG Init")
 *  field(SNAM, "WG Gen")
 *  field(FTA , "STRING")
 *  field(FTB , "DOUBLE")
 *  field(FTC , "DOUBLE")
 *  field(FTVB, "DOUBLE")
 *  field(NOC , "128")
 *  field(NOVB, "128")
 *  field(INPA, "$(N):expr NPP")
 *  field(INPB, "someacalar")
 *  field(INPC, "somearray")
 *  field(OUTB, "outarray PP")
 * }
 *
 * Each element of the output is computed from the corresponding
 * element of each input.  For arrays of a given length the last
 * value read (or 0.0 for empty arrays) is used.
 *
 * For example, B*C
 * Gives different results depending in the length of its inputs.
 *
 * B = [1,2,3]
 * C = [1,2,3]
 * OUTB = [1,4,9]
 *
 * B = [1,2,3]
 * C = [2]
 * OUTB = [2,4,6]
 *
 * B = [1,2,3]
 * C = [2,3]
 * OUTB = [2,6,9]
 *
 * Note: To support longer expressions set FTA=CHAR and NOA<=100
 */

struct calcPriv {
    char prev[MAX_INFIX_SIZE];
    char postfix[MAX_POSTFIX_SIZE];
    double stack[CALCPERFORM_NARGS];
    epicsUInt32 usein, useout; // can be used
    unsigned long ins, outs;   // will be used
    unsigned long val; // output used for the expression result

    double *curin[CALCPERFORM_NARGS],
    *lastin[CALCPERFORM_NARGS],
    *curout[CALCPERFORM_NARGS],
    *lastout[CALCPERFORM_NARGS];
};
typedef struct calcPriv calcPriv;

// must be enough bits to represent all arguments in 'usein' and 'useout'
STATIC_ASSERT(sizeof(epicsUInt32)*8 >= CALCPERFORM_NARGS);

static
long update_expr(aSubRecord* prec, calcPriv *priv)
{
    short err;
    unsigned long val;
    size_t inlen;
    if(prec->fta==menuFtypeCHAR && prec->nea<MAX_INFIX_SIZE)
        inlen=prec->nea;
    else if(prec->fta==menuFtypeCHAR)
        inlen=MAX_INFIX_SIZE-1;
    else
        inlen=strlen((char*)prec->a);

    if(strlen(priv->prev)==inlen && strncmp(priv->prev, (char*)prec->a, inlen)==0)
        return 0; /* no change */

    memcpy(priv->prev, (char*)prec->a, inlen);
    priv->prev[inlen]='\0';

    if(strlen(priv->prev)==0)
        strcpy(priv->prev, "0");

    if(postfix(priv->prev, priv->postfix, &err)) {
        (void)recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);

        errlogPrintf("%s: Expression error: %s from '%s'\n",
                     prec->name, calcErrorStr(err), priv->prev);
        return -1;
    }
    calcArgUsage(priv->postfix, &priv->ins, &priv->outs);

    priv->val = ~priv->outs;   // consider unassigned outputs
    priv->val &= priv->useout; // exclude unuseable

    val = priv->val & (priv->val-1); // clear lowest bit which is set
    priv->val = val ^ priv->val; // pick only lowest bit which is set

    return 0;
}

static
long init_waveform(aSubRecord* prec)
{
    size_t i;
    calcPriv* priv;

    if(prec->fta != menuFtypeSTRING && prec->fta!=menuFtypeCHAR) {
        errlogPrintf("%s: FTA must be STRING or CHAR\n", prec->name);
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return -1;
    }

    priv = calloc(1, sizeof(calcPriv));
    if(!priv) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return -1;
    }
    strcpy(priv->prev, "Something highly unlikely");

    for(i=0; i<CALCPERFORM_NARGS; i++) {
        if((&prec->fta)[i]==menuFtypeDOUBLE)
            priv->usein |= (1<<i);

        if((&prec->ftva)[i]==menuFtypeDOUBLE)
            priv->useout |= (1<<i);
    }

    if(priv->usein==0) {
        errlogPrintf("%s: No input arguments???", prec->name);
    }
    if(priv->useout==0) {
        errlogPrintf("%s: No output arguments???", prec->name);
    }

    prec->dpvt = priv;
    return 0;
}

static
long gen_waveform(aSubRecord* prec)
{
    size_t i, s;
    epicsUInt32 nelem=0, nin=0;
    calcPriv* priv=prec->dpvt;
    if(!priv) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        errlogPrintf("%s: Not initialized\n",prec->name);
        return -1;
    }

    if(update_expr(prec, priv)) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return -1;
    }

    // reset stack
    memset(&priv->stack, 0, sizeof(priv->stack));

    // initialize input pointers
    for(i=0; i<CALCPERFORM_NARGS; ++i) {
        priv->curin[i]  = ((double**)&prec->a)[i];
        priv->curout[i] = ((double**)&prec->vala)[i];
        priv->lastin[i] = priv->curin[i] + (&prec->nea)[i];
        priv->lastout[i]= priv->curout[i]+ (&prec->nova)[i];

        nelem= MAX(nelem,(&prec->nova)[i]);
        nin  = MAX(nin  ,(&prec->nea)[i]);
    }

    nelem = MIN(nelem, nin);

    // A is taken by the calc expression so use it to store PI
    priv->stack[0] = PI;
    priv->curin[0] = priv->lastin[0] = 0;

    for(s=0; s<nelem; s++) {
        double val;
        // for each output sample

        // update stack from inputs
        for(i=1; i<CALCPERFORM_NARGS; ++i) {
            if(!(priv->usein & (1<<i)))
                continue;
            if(priv->curin[i] >= priv->lastin[i])
                continue;

            priv->stack[i] = *(priv->curin[i])++;
        }

        // calculate
        if (calcPerform(priv->stack, &val, priv->postfix)) {
            recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        } else prec->udf = isnan(val);

        // update outputs from stack
        for(i=0; i<CALCPERFORM_NARGS; ++i) {
            if(!(priv->useout & (1<<i)))
                continue;
            if(priv->curout[i] >= priv->lastout[i])
                continue;

            // place the expression result or the stack value
            if(priv->val == (1<<i))
                *(priv->curout[i])++ = val;

            else
                *(priv->curout[i])++ = priv->stack[i];
        }

    }

    for(i=0; i<CALCPERFORM_NARGS; ++i) {
        if(!(priv->useout & (1<<i)))
            continue;
        (&prec->neva)[i] = MIN( nelem, (&prec->nova)[i]);
    }

    return 0;
}

/* I/Q to amplitude/phase converter.
 * Also provide amplitude squared (power)
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "IQ2AP")
 *  field(FTA , "DOUBLE")
 *  field(FTB , "DOUBLE")
 *  field(FTC , "DOUBLE")
 *  field(FTD , "DOUBLE")
 *  field(FTVA ,"DOUBLE")
 *  field(FTVB ,"DOUBLE")
 *  field(FTVC ,"DOUBLE")
 *  field(NOA , "128")
 *  field(NOB , "128")
 *  field(NOVA, "128")
 *  field(NOVB, "128")
 *  field(NOVC, "128")
 *  field(INPA, "I")
 *  field(INPB, "Q")
 *  field(INPC, "POWSCALE") # scaling of AMP squared (Optional)
 *  field(INPD, "ZRANGLE") # arbitrary angle added to output phase (deg.)
 *  field(INPE, "DISPANGLE") # rotate I/Q for displays
 *  field(OUTA, "AMP PP")
 *  field(OUTB, "PHA PP") # in degrees
 *  field(OUTC, "POW PP") # AMP squared (Optional)
 *  field(OUTD, "IWFDISP PP")
 *  field(OUTE, "QWFDISP PP")
 * }
 */
#define MAGIC ((void*)&convert_iq2ap)
#define BADMAGIC ((void*)&gen_waveform)

static
long convert_iq2ap(aSubRecord* prec)
{
    size_t i;
    epicsUInt32 len = prec->nea; /* actual output length */
    unsigned pow_out = prec->ftvc==menuFtypeDOUBLE;
    unsigned rot_out = prec->ftvd==menuFtypeDOUBLE && prec->ftve==menuFtypeDOUBLE;
    double pow_scale = 1.0, zero_angle = 0.0, disp_angle = 0.0;

    double *I = (double*)prec->a,
           *Q = (double*)prec->b,
           *A = (double*)prec->vala,
           *P = (double*)prec->valb,
           *PW= pow_out ? (double*)prec->valc : NULL,
           *IROT = rot_out ? (double*)prec->vald : NULL,
           *QROT = rot_out ? (double*)prec->vale : NULL;

    if(prec->fta!=menuFtypeDOUBLE
            || prec->ftb!=menuFtypeDOUBLE
            || prec->ftva!=menuFtypeDOUBLE
            || prec->ftvb!=menuFtypeDOUBLE
            || prec->ftvd!=menuFtypeDOUBLE
            || prec->ftve!=menuFtypeDOUBLE)
    {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    }

    if(prec->ftc==menuFtypeDOUBLE) {
        pow_scale = *(double*)prec->c;
    }
    if(pow_scale==0.0) {
        pow_scale = 1.0;
    }

    if(prec->ftd==menuFtypeDOUBLE) {
        zero_angle = *(double*)prec->d;
    }

    if(prec->fte==menuFtypeDOUBLE) {
        disp_angle = *(double*)prec->e*PI/180.0;
    }

    if(len > prec->neb)
        len = prec->neb;
    if(len > prec->nova)
        len = prec->nova;
    if(len > prec->novb)
        len = prec->novb;
    if(pow_out && len > prec->novc)
        len = prec->novc;
    if(rot_out && len > prec->novd)
        len = prec->novd;
    if(rot_out && len > prec->nove)
        len = prec->nove;

    for(i=0; i<len; i++) {
        A[i] = sqrt(I[i]*I[i] + Q[i]*Q[i]);
        P[i] = atan2(Q[i], I[i]) * 180 / PI;
        P[i] = phase_wrap(P[i] + zero_angle);
        if(PW)
            PW[i] = pow_scale * A[i] * A[i];
        if(rot_out) {
            IROT[i] = I[i]*cos(disp_angle) + Q[i]*sin(disp_angle);
            QROT[i] = - I[i]*sin(disp_angle) + Q[i]*cos(disp_angle);
        }
    }

    prec->neva = prec->nevb = prec->nevd = prec->neve = len;
    if(pow_out)
        prec->nevc = len;
    if(rot_out) {
        prec->nevd = prec->neve = len;
    }

    return 0;
}

/* Amplitude/phase to I/Q converter
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "AP2IQ")
 *  field(FTA , "DOUBLE")
 *  field(FTB , "DOUBLE")
 *  field(FTVA ,"DOUBLE")
 *  field(FTVB ,"DOUBLE")
 *  field(NOA , "128")
 *  field(NOB , "128")
 *  field(NOVA, "128")
 *  field(NOVB, "128")
 *  field(INPA, "AMP")
 *  field(INPB, "PHA")
 *  field(OUTA, "I PP")
 *  field(OUTB, "Q PP")
 * }
 */

static
long convert_ap2iq(aSubRecord* prec)
{
    size_t i;
    epicsEnum16 *ft = &prec->fta,
            *ftv= &prec->ftva;
    // actual length of inputs, and max length of outputs
    epicsUInt32 lens[4] = { prec->nea, prec->neb, prec->nova, prec->novb };
    epicsUInt32 len = lens[0];


    if(prec->dpvt==BADMAGIC) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    } else if(prec->dpvt!=MAGIC) {
        // Only do type checks in not already passed
        for(i=0; i<2; i++) {
            if(ft[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
                errlogPrintf("%s: FT%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            } else if(ftv[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
                errlogPrintf("%s: FTV%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            }
        }
        prec->dpvt = MAGIC;
    }

    for(i=0; i<4; i++)
        len = MIN(len, lens[i]);

    double *I = (double*)prec->vala,
            *Q = (double*)prec->valb,
            *A = (double*)prec->a,
            *P = (double*)prec->b;

    for(i=0; i<len; i++) {
        I[i] = A[i] * cos(P[i] * PI / 180.0);
        Q[i] = A[i] * sin(P[i] * PI / 180.0);
    }

    prec->neva = prec->nevb = len;

    return 0;
}

/* Waveform statistics
 *
 * Computes mean and std of the (subset of)
 * waveform Y.  The values of waveform X
 * (aka time) are used to compute the windows
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "Wf Stats")
 *  field(FTA , "DOUBLE")
 *  field(FTB , "DOUBLE")
 *  field(FTC , "DOUBLE")
 *  field(FTD , "DOUBLE")
 *  field(FTE , "LONG")
 *  field(FTVA ,"DOUBLE")
 *  field(FTVB ,"DOUBLE")
 *  field(FTVC ,"DOUBLE")
 *  field(FTVD ,"DOUBLE")
 *  field(NOA , "128")
 *  field(NOB , "128")
 *  field(INPA, "Waveform Y")
 *  field(INPB, "Waveform X")
 *  field(INPC, "Start X") # window start
 *  field(INPD, "Width X") # window width
 *  field(INPE, "isphase")  # optional, 1 - output modulo +-180
 *  field(OUTA, "MEAN PP")
 *  field(OUTB, "STD PP")
 *  field(OUTC, "MIN PP")
 *  field(OUTD, "MAX PP")
 *  field(OUTE, "RSTD PP")
 */

static
long wf_stats(aSubRecord* prec)
{
    size_t i, N=0;
    epicsEnum16 *ft = &prec->fta,
                *ftv= &prec->ftva;
    epicsUInt32 phamod = prec->fte==menuFtypeLONG ? *(epicsUInt32*)prec->e : 0;
    // actual length of inputs
    epicsUInt32 len = MIN(prec->nea, prec->neb);

    double *data = prec->a,
            *time = prec->b,
            sum   = 0.0,
            sum2  = 0.0,
            start = *(double*)prec->c,
            width = *(double*)prec->d;
    double  min = epicsNAN, max = epicsNAN;


    if(prec->dpvt==BADMAGIC) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    } else if(prec->dpvt!=MAGIC) {
        // Only do type checks in not already passed
        for(i=0; i<4; i++) {
            if(ft[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
                errlogPrintf("%s: FT%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            }
        }
        for(i=0; i<4; i++) {
            if(ftv[i]!=menuFtypeDOUBLE) {
                prec->dpvt=BADMAGIC;
                (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
                errlogPrintf("%s: FTV%c must be DOUBLE\n",
                             prec->name, 'A'+(char)i);
                return 1;

            }
        }
        prec->dpvt = MAGIC;
    }

    if(len==0) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }

    for(i=0; i<len; i++) {
        if(time[i]<start)
            continue;
        if(time[i]>=start+width)
            break;

        if(min > data[i] || N==0)
            min = data[i];
        if(max < data[i] || N==0)
            max = data[i];

        sum  += data[i];
        sum2 += data[i] * data[i];
        N++;
    }

    if(N==0) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }

    sum  /= N; // <x>
    sum2 /= N; // <x^2>

    *(double*)prec->vala = sum;
    prec->neva=1;
    *(double*)prec->valb = sqrt(sum2 - sum*sum);
    prec->nevb=1;

    if(phamod) {
        /* (re)wrap as phase.
         * Only min/max/mean, not sure how to wrap std meaningfully.
         */
        *(double*)prec->vala = phase_wrap(*(double*)prec->vala);
        min = phase_wrap(min);
        max = phase_wrap(max);
        if(max < min) {
            /* make sure we don't confuse users if max wraps to less than min */
            double temp = min;
            min = max;
            max = temp;
        }
    }

    *(double*)prec->valc = min;
    prec->nevc=1;
    *(double*)prec->vald = max;
    prec->nevd=1;
    *(double*)prec->vale = *(double*)prec->valb / fabs(*(double*)prec->vala);
    prec->neve=1;

    return 0;
}

/* Unwrap phase
 *
 * Takes a phase signal which may be wrapped around [-180, 180].
 * Unwrap for jumps where the unwrapped phase would not change
 * by more then Max Difference degrees between samples.
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "Phase Unwrap")
 *  field(FTA , "DOUBLE")
 *  field(NOA , "128")
 *  field(NOVA, "128")
 *  field(INPA, "Wrapped phase")
 *  field(INPB, "Max Difference")
 *  field(OUTA, "Unwrapped phase PP")
 */

static
long unwrap(aSubRecord* prec)
{
    size_t i;
    double *in = (double*)prec->a,
            *out= (double*)prec->vala,
            thres;
    epicsUInt32 len=MIN(prec->nea, prec->nova);

    if(prec->dpvt==BADMAGIC) {
        (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
        return 1;
    } else if(prec->dpvt!=MAGIC) {
        // Only do type checks in not already passed
        if(prec->fta!=menuFtypeDOUBLE &&
                prec->ftb!=menuFtypeDOUBLE) {
            prec->dpvt=BADMAGIC;
            (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
            errlogPrintf("%s: FTA and FTB must be DOUBLE\n",
                         prec->name);
            return 1;

        }
        if(prec->ftva!=menuFtypeDOUBLE) {
            prec->dpvt=BADMAGIC;
            (void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
            errlogPrintf("%s: FTVA must be DOUBLE\n",
                         prec->name);
            return 1;

        }
        prec->dpvt = MAGIC;
    }

    if(len==0 || prec->neb==0) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }
    thres = *(double*)prec->b;
    thres /= 2.0; // half on either side of the fold

    out[0]=in[0]; // start at same point

    for(i=1; i<len; i++) {
        double delta = in[i] - in[i-1];

        // the following will only work for wrapping
        // at shallow angles

        // wrapped from positive to negative
        if(in[i-1]>(180.0-thres) && in[i]<(-180.0+thres))
            delta += 360.0;

        // wrapped from negative to positive
        else if(in[i-1]<(-180.0+thres) && in[i]>(180.0-thres))
            delta += -360.0;

        out[i] = out[i-1] + delta;
    }

    prec->neva = len;
    return 0;
}

/* Calculate PI controller output */
static
long calc_ctrl(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;

	size_t i;
	epicsUInt32 len = prec->neb; /* actual output length */
	double sel_poff = 0.0, gain = 1.0;

	double *DACI = (double*)prec->b,
		*DACQ  = (double*)prec->c,
		*CAVP  = (double*)prec->d,
		*CTRLI = (double*)prec->vala,
		*CTRLQ = (double*)prec->valb,
		*CTRLP = (double*)prec->valc,
		*CTRLA = (double*)prec->vald,
		*DACP  = (double*)prec->vale,
		*ROTP  = (double*)prec->valf;

	if(prec->ftb!=menuFtypeDOUBLE
		|| prec->ftc!=menuFtypeDOUBLE
		|| prec->ftd!=menuFtypeDOUBLE
		|| prec->ftva!=menuFtypeDOUBLE
		|| prec->ftvb!=menuFtypeDOUBLE
		|| prec->ftvc!=menuFtypeDOUBLE
		|| prec->ftvd!=menuFtypeDOUBLE
		|| prec->ftve!=menuFtypeDOUBLE
		|| prec->ftvf!=menuFtypeDOUBLE)
	{
		(void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		return 1;
	}

	if(prec->fta==menuFtypeDOUBLE) {
		sel_poff = *(double*)prec->a;
	}

	if(prec->fte==menuFtypeDOUBLE) {
		gain = *(double*)prec->e;
	}

	epicsTimeStamp daci, dacq, cavp;
	double t1, t2;
    dbGetTimeStamp(&prec->inpb, &daci);
    dbGetTimeStamp(&prec->inpc, &dacq);
    dbGetTimeStamp(&prec->inpd, &cavp);
    t1 =epicsTimeDiffInSeconds(&daci,&dacq);
    t2 =epicsTimeDiffInSeconds(&daci,&cavp);
    if ( (0.0 != t1) || (0.0 != t2) ) {
        if ( debug ) {
            printf("calc_ctrl %s: I, Q, amplitude timestamps do not match: deltas %.2f s %.2f s\n",
            prec->name, t1, t2);
        }
        /* Do not process outputs */
        return -1;
    }
	double rot = 0.0;

	if(len > prec->nec)
		len = prec->nec;
	if(len > prec->ned)
		len = prec->ned;
	if(len > prec->nova)
		len = prec->nova;
	if(len > prec->novb)
		len = prec->novb;
	if(len > prec->novc)
		len = prec->novc;
	if(len > prec->novd)
		len = prec->novd;
	if(len > prec->nove)
		len = prec->nove;
	if(len > prec->novf)
		len = prec->novf;

	for(i=0; i<len; i++) {
		ROTP[i] = CAVP[i] - sel_poff;
		rot = (ROTP[i])* PI/180; 
		CTRLI[i] = (DACI[i]*cos(-rot) - DACQ[i]*sin(-rot))*gain;
		CTRLQ[i] = -((DACI[i]*sin(-rot) + DACQ[i]*cos(-rot))*gain);
        CTRLP[i] = atan2(CTRLQ[i], CTRLI[i]) * 180 / PI;
        CTRLA[i] = sqrt(pow(CTRLI[i],2) + pow(CTRLQ[i],2));
        DACP[i] = atan2(DACQ[i], DACI[i]) * 180 / PI;
        if ( debug ) {
			printf("rot %f p %f selpoff %f i %f q %f p %f a %f gain %f\n",
				rot, CAVP[i], sel_poff, CTRLI[i], CTRLQ[i], CTRLP[i], CTRLA[i], gain);
		}
	}

	prec->neva = prec->nevb = prec->nevc = prec->nevd = prec->neve = prec->nevf = len;

	return 0;
}

static
long ctrl_lims(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;

	/* Amplitude/phase control limit register values */
	epicsInt32  amp_l_in = *(epicsInt32*)prec->a,
				amp_h_in = *(epicsInt32*)prec->b,
				pha_l_in = *(epicsInt32*)prec->c,
				pha_h_in = *(epicsInt32*)prec->d;

	/* Normalized limits for graphic */
	double  *amp = (double*)prec->vala,
		    *pha = (double*)prec->valb,
			*amp_l = (double*)prec->valc,
			*amp_h = (double*)prec->vald,
			*pha_l = (double*)prec->vale,
	  *pha_h = (double*)prec->valf,
	  *amp_l_line = (double*)prec->valg,
	  *amp_h_line = (double*)prec->valh,
	  *pha_l_line = (double*)prec->vali,
	  *pha_h_line = (double*)prec->valj;

	double amp_l_disp, amp_h_disp, pha_l_disp, pha_h_disp;

	epicsUInt32 len = prec->nee; /* actual output length */

	double cordic = 1.64676; /* From Larry */

	int i;

	*amp_l = amp_l_disp = (double)amp_l_in * cordic/(131072.0);
	*amp_h = amp_h_disp = (double)amp_h_in * cordic/(131071.0);
	*pha_l = pha_l_disp = (double)pha_l_in * cordic/(131072.0);	
	*pha_h = pha_h_disp = (double)pha_h_in * cordic/(131071.0);

	/* If drive limits equal, add some width
	 * for visual aid
	 */
	if ( *amp_l == *amp_h ) {
		amp_l_disp = *amp_l * 1.01;
		amp_h_disp = *amp_h * 0.99;
	}
	if ( *pha_l == *pha_h ) {
		pha_l_disp = *pha_l + .01;
		pha_h_disp = *pha_h - .01;
	}
	amp[0] = amp[1] = amp[4] = amp_l_disp;
	amp[2] = amp[3] = amp_h_disp;
	pha[0] = pha[3] = pha[4] = pha_l_disp;
	pha[1] = pha[2] = pha_h_disp;

	for ( i = 0; i < len; i++ ) {
	  amp_l_line[i] = amp_l_disp;
	  amp_h_line[i] = amp_h_disp;
	  pha_l_line[i] = pha_l_disp;
	  pha_h_line[i] = pha_h_disp;	  
	}

	if ( debug ) {
		printf("%s: amp l %f h %f pha l %f h %f\n",
		prec->name, *amp_l, *amp_h, *pha_l, *pha_h);
		printf("%s: disp amp l %f h %f pha l %f h %f\n",
		prec->name, amp_l_disp, amp_h_disp, pha_l_disp, pha_h_disp);
	}

	prec->nevg = prec->nevh = prec->nevi = prec->nevj = len;

	return 0;
}

/* Detuning waveform calculator
 * This is supposed to mirror the calculation done in the FPGA's digaree module
 * based on piezo_sf_consts.  The BCOEF complex number provided here is in units 
 * of 1/s, matches the value given to digaree in piezo_sf_consts 
 */
static
long calc_df(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;

	size_t i;
	epicsUInt32 len = prec->nea; /* actual output length */
	double bcoefm = 0.0, bcoefp = 0.0,
		cavscl = 1.0, fwdscl = 1.0, sampt = 1.0;

	double complex dvdt_cmplx, bcoef_cmplx, fwd_cmplx, cav_cmplx, a_cmplx;

	double *CAVI = (double*)prec->a,
		*CAVQ  = (double*)prec->b,
		*FWDI  = (double*)prec->c,
		*FWDQ = (double*)prec->d,
		*DF = (double*)prec->vala,
		*BW = (double*)prec->valb,
		*DVDTI = (double*)prec->valc,
		*DVDTQ = (double*)prec->vald;

	if(prec->fta!=menuFtypeDOUBLE
		|| prec->ftb!=menuFtypeDOUBLE
		|| prec->ftc!=menuFtypeDOUBLE
		|| prec->ftd!=menuFtypeDOUBLE
		|| prec->ftva!=menuFtypeDOUBLE
		|| prec->ftvb!=menuFtypeDOUBLE
		|| prec->ftvc!=menuFtypeDOUBLE
		|| prec->ftvd!=menuFtypeDOUBLE)
	{
		(void)recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM);
		return 1;
	}

	if(prec->fte==menuFtypeDOUBLE) {
		bcoefm = *(double*)prec->e;
	}
	if(prec->ftf==menuFtypeDOUBLE) {
		bcoefp = *(double*)prec->f;
	}
	if(prec->ftg==menuFtypeDOUBLE) {
		cavscl = *(double*)prec->g;
	}
	if(prec->fth==menuFtypeDOUBLE) {
		fwdscl = *(double*)prec->h;
	}
	if(prec->fti==menuFtypeDOUBLE) {
		sampt = *(double*)prec->i;
	}

	epicsTimeStamp cavi, cavq, fwdi, fwdq;
	double t1, t2, t3;
    dbGetTimeStamp(&prec->inpa, &cavi);
    dbGetTimeStamp(&prec->inpb, &cavq);
    dbGetTimeStamp(&prec->inpc, &fwdi);
    dbGetTimeStamp(&prec->inpd, &fwdq);
    t1 =epicsTimeDiffInSeconds(&cavi,&cavq);
    t2 =epicsTimeDiffInSeconds(&fwdi,&cavq);
    t3 =epicsTimeDiffInSeconds(&fwdq,&cavq);
    if ( (0.0 != t1) || (0.0 != t2) || (0.0 != t3) ) {
		if ( debug ) {
	    	printf("calc_df %s: Cav, Fwd, I, Q, amplitude timestamps do not all match: deltas %.2f s %.2f s %.2f s\n",
	    	prec->name, t1, t2, t3);
		}
		/* Do not process outputs */
		return -1;
    }

	if(len > prec->neb)
		len = prec->neb;
	if(len > prec->nec)
		len = prec->nec;
	if(len > prec->ned)
		len = prec->ned;
	if(len > prec->nova)
		len = prec->nova;
	if(len > prec->novb)
		len = prec->novb;
	if(len > prec->novc)
		len = prec->novc;
	if(len > prec->novd)
		len = prec->novd;

	/* investigate writing this as bcoefm * exp(bcoefp*PI/180*_Complex_I) */
	bcoef_cmplx = bcoefm * cos(bcoefp*PI/180.0)  + (bcoefm * sin(bcoefp*PI/180.0))*_Complex_I;
	if ( debug ) {
		printf("\nbcoefm %f bcoefp %f bcoef_cmplx %f +i %f sampt %f cavscl %fwdscl %f\n", 
				bcoefm, bcoefp, creal(bcoef_cmplx), cimag(bcoef_cmplx), sampt, cavscl, fwdscl);
	}

	for (i=0; i<len; i++) {
		if ( i < len - 1 ) {
			DVDTI[i] = (CAVI[i+1] - CAVI[i])/sampt/cavscl;
			DVDTQ[i] = (CAVQ[i+1] - CAVQ[i])/sampt/cavscl;
		}
		else {
			DVDTI[i] = DVDTI[i-1];
			DVDTQ[i] = DVDTQ[i-1];
		}
		dvdt_cmplx = DVDTI[i] + DVDTQ[i]*_Complex_I;
		fwd_cmplx = FWDI[i]/fwdscl + (FWDQ[i]/fwdscl)*_Complex_I;
		cav_cmplx = CAVI[i]/cavscl + (CAVQ[i]/cavscl)*_Complex_I;
		a_cmplx= (dvdt_cmplx - (bcoef_cmplx * fwd_cmplx)) / (2 * PI *cav_cmplx);  /* give output in Hz, not radians/sec */

		DF[i] = cimag( a_cmplx);
		BW[i] = -creal( a_cmplx);
		/* Todo: remove this debug print: */
		if ( debug ) {
			printf("DVDTI %f DVDTQ %f FWDI %f FWDQ %f CAVI %f CAVQ %f fwd %f +i "
					"%f cav %f +i %f a %f +i %f DF %f BW %f cavscl %f fwdscl %f\n",
					DVDTI[i], DVDTQ[i], FWDI[i], FWDQ[i], CAVI[i], CAVQ[i], creal(fwd_cmplx), 
					cimag(fwd_cmplx), creal(cav_cmplx), cimag(cav_cmplx), 
					creal(a_cmplx), cimag(a_cmplx), DF[i], BW[i], cavscl, fwdscl);
		}
	}

	prec->neva = prec->nevb = prec->nevc = prec->nevd = len;

	return 0;
}

/* Unwrap Fault
 *
 * Fault waveform from circle buffer needs to be unwrapped in order
 * to recover a casual time scale, based on information from buffer
 * status of start_address and wrapped bit.
 *
 * If wrapped, means write is faster than read, simply np.roll the
 * waveform * by -offset, where offset = start_address / 2*n_chan_keep.
 * If not wrapped, means read is faster than write (shouldn't happen),
 * full waveform needs to be concatenated by two bank of buffers.
 * Hence the bank has to be flipped and read twice. This is not easy
 * to implement in current structure. TBD
 *
 * record(aSub, "$(N)") {
 *  field(SNAM, "Fault Unwrap")
 *  field(FTA , "DOUBLE")
 *  field(FTB , "USHORT")
 *  field(FTC , "USHORT")
 *  field(FTD , "DOUBLE")
 *  field(NOA , "128")
 *  field(NOVA, "128")
 *  field(INPA, "Wrapped Fault WF")
 *  field(INPB, "Offset")
 *  field(INPC, "Wrapped bit")
 *  field(INPD, "Last Wrapped Fault WF")
 *  field(OUTA, "Unwrapped Fault WF")
 */

static
long unwrap_fault(aSubRecord* prec)
{
	short debug = (prec->tpro > 1) ? 1 : 0;

    double *in = (double*)prec->a,
           *in_last = (double*) prec->d,
            *out= (double*)prec->vala;

    epicsUInt16 i;
    epicsUInt16 offset;
    epicsUInt16 wrap;

    epicsUInt32 len=MIN(prec->nea, prec->nova);

    if(prec->ftb!=menuFtypeUSHORT) {
        errlogPrintf("%s: B has to be type USHORT\n", prec->name);
        return 1;
    }

    if(len==0) {
        recGblSetSevr(prec, CALC_ALARM, INVALID_ALARM);
        return 0;
    }

    offset = *(epicsUInt16*)prec->b;
    wrap = *(epicsUInt16*)prec->c & 1;

	if ( debug ) {
		errlogPrintf("%s: len=%d, offset=%u, wrap=%u\n", prec->name, len, offset, wrap);
	}

    //offset;

    // Copy data from hardware to software
    //
    // In the fault (frozen) condition, the hardware buffer is split as
    //   (possibly) early-time data segment [offset, len-1]
    //   and (always) late-time data segment [0, offset-1]
    // Assemble that into the output buffer, where
    //   early time is [0, len-offset-1]
    //   and late time is [len-offset, len-1]
    // If the wrap bit is _not_ set, the early time data comes instead
    //   from the previous buffer [offset, len-1]
    //
    // Handle the late-time segment
    for(i=0; i<offset; i++) {
        out[len-offset+i] = in[i];
    }
    // Hnandle the early-time segment
    for(i=offset; i<len; i++) {
        out[i-offset] = wrap ? in[i] : in_last[i];
    }

    prec->neva = len;
    return 0;
}

static registryFunctionRef asub_seq[] = {
    {"IQ2AP", (REGISTRYFUNCTION) &convert_iq2ap},
    {"AP2IQ", (REGISTRYFUNCTION) &convert_ap2iq},
    {"WG Gen", (REGISTRYFUNCTION) &gen_waveform},
    {"WG Init", (REGISTRYFUNCTION) &init_waveform},
    {"Wf Stats", (REGISTRYFUNCTION) &wf_stats},
    {"Phase Unwrap", (REGISTRYFUNCTION) &unwrap},
    {"Controller Output", (REGISTRYFUNCTION) &calc_ctrl},
    {"Controller Limits", (REGISTRYFUNCTION) &ctrl_lims},
	{"Calculate Detune", (REGISTRYFUNCTION) &calc_df},
    {"Fault Unwrap", (REGISTRYFUNCTION) &unwrap_fault},
};

static
void rfcalcRegister(void) {
    registryFunctionRefAdd(asub_seq, NELEMENTS(asub_seq));
}

#include <epicsExport.h>

epicsExportRegistrar(rfcalcRegister);
