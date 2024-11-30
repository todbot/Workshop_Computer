#ifndef DIVIDER_H
#define DIVIDER_H

////////////////////////////////////////
// Clock divider


typedef struct
{
	uint8_t counter;
	uint8_t divisor;
} divider;

// Call with risingEdge true/false on a trigger. Returns new state of clock-divided trigger
// with risingEdge true, returns true only every <divisor>th time it is called
// with risingEdge false, returns true only if divisor>1 and last risingEdge was true
bool divider_step(divider *d, bool risingEdge)
{
	if (risingEdge)
	{
		d->counter++;
		if (d->counter == d->divisor)
		{
			d->counter = 0;
			return true;
		}
		else return false;
	}
	else
	{
		if (d->divisor==1)
			return false;
		else
			return d->counter==0;
	}
}

void divider_set(divider *d, uint8_t divisor)
{
	d->divisor = divisor;
	if (d->counter >= d->divisor)
		d->counter = d->divisor-1;
}

void divider_init(divider *d)
{
	divider_set(d, 1);
}
#endif
