// Runs against Renegade's own Miles 6 DLL/providers. No sound is played.
#include "mss.h"
#include <stdio.h>

int main()
{
	if (!AIL_startup()) { puts("FAIL: Miles startup"); return 1; }
	PCMWAVEFORMAT format = {};
	format.wf.wFormatTag = WAVE_FORMAT_PCM;
	format.wf.nChannels = 2;
	format.wf.nSamplesPerSec = 44100;
	format.wBitsPerSample = 16;
	format.wf.nBlockAlign = 4;
	format.wf.nAvgBytesPerSec = 176400;
	HDIGDRIVER driver = NULL;
	if (AIL_waveOutOpen(&driver, NULL, 0, (LPWAVEFORMAT)&format) != 0) {
		printf("FAIL: device: %s\n", AIL_last_error()); return 1;
	}
	HPROENUM next = HPROENUM_FIRST;
	HPROVIDER provider = 0;
	char *name = NULL;
	int checked = 0, failures = 0;
	while (AIL_enumerate_3D_providers(&next, &provider, &name)) {
		if (AIL_open_3D_provider(provider) != 0) continue;
		H3DSAMPLE sample = AIL_allocate_3D_sample_handle(provider);
		if (sample) {
			int values[] = {0, 1, 32, 64, 127};
			for (int volume : values) {
				AIL_set_3D_sample_volume(sample, volume);
				int result = AIL_3D_sample_volume(sample);
				printf("%s: set %d -> get %d %s\n", name, volume, result, volume == result ? "PASS" : "FAIL");
				if (volume != result) ++failures;
			}
			++checked;
			AIL_release_3D_sample_handle(sample);
		}
		AIL_close_3D_provider(provider);
	}
	AIL_waveOutClose(driver);
	printf("Providers checked: %d; failures: %d\n", checked, failures);
	return checked > 0 && failures == 0 ? 0 : 1;
}
