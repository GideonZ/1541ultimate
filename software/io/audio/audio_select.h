#ifndef AUDIO_SELECT_H
#define AUDIO_SELECT_H

#define AUDIO_SELECT_LEFT   *((volatile BYTE *)0x4060700)
#define AUDIO_SELECT_RIGHT  *((volatile BYTE *)0x4060701)

#define SOUND_DRIVE_0     0
#define SOUND_DRIVE_1     1
#define SOUND_CAS_READ    2
#define SOUND_CAS_WRITE   3

#endif
