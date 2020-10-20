#include "filesystem_d64.h"
#include "dump_hex.h"

#ifndef SS_DEBUG
#define SS_DEBUG 0
#endif

class SideSectorCluster
{
    int datablocks;
    uint8_t record_size;
    CachedBlock *blocks[6];
    FileSystemCBM *fs;
    uint8_t start_track;
    uint8_t start_sector;
    bool faulty;
public:
    SideSectorCluster(FileSystemCBM *fs, uint8_t recordSize) {
        start_track = 0;
        start_sector = 0;
        datablocks = 0;
        record_size = recordSize;
        faulty = false;
        this->fs = fs;
        for(int i=0; i<6; i++) {
            blocks[i] = 0;
        }
    }
    ~SideSectorCluster() {
        for (int i=0;i<6;i++) {
            if (blocks[i]) {
                delete blocks[i];
            }
        }
    }

    bool addDataBlock(uint8_t track, uint8_t sector)
    {
        int t=0,s=0;

        if (datablocks == 720) {
            return false; // full
        }
        int side_sector_index = datablocks / 120;
        int side_sector_offset = datablocks - (side_sector_index * 120);

        if (!blocks[side_sector_index]) {
            if (!fs->get_next_free_sector(t, s)) {
                return false;
            }
            if (side_sector_index == 0) {
                start_track = t;
                start_sector = s;
            }
            CachedBlock *newBlock = new CachedBlock(256, t, s);
            blocks[side_sector_index] = newBlock;
            newBlock->data[2] = (uint8_t)side_sector_index;
            newBlock->data[3] = record_size;
        }
        uint8_t *data = blocks[side_sector_index]->data;
        data[16 + 2*side_sector_offset] = track;
        data[17 + 2*side_sector_offset] = sector;
        datablocks ++;
        return true;
    }

    void get_track_sector(int& track, int& sector)
    {
        if (blocks[0]) {
            track = blocks[0]->track;
            sector = blocks[0]->sector;
        } else {
            track = 0;
            sector = 0;
        }
    }

    int get_number_of_data_blocks(void)
    {
        return datablocks;
    }

    CachedBlock *validate(uint8_t rec_size)
    {
        if(rec_size) {
            record_size = rec_size;
        }

        uint8_t local_references[12];
        memset(local_references, 0, 12);
        for(int i=0;i<6;i++) {
            if (blocks[i]) {
                local_references[0+2*i] = blocks[i]->track;
                local_references[1+2*i] = blocks[i]->sector;
            }
        }
        CachedBlock *last = NULL;
        int remaining = datablocks;
        for(int i=0;i<6;i++) {
            if (blocks[i]) {
                last = blocks[i];
                memcpy(blocks[i]->data + 4, local_references, 12);
                blocks[i]->data[2] = (uint8_t)i;
                blocks[i]->data[3] = record_size;
                blocks[i]->data[1] = (uint8_t)((remaining * 2) + 15);
            }
            if ((i != 5) && (blocks[i+1])) {
                blocks[i]->data[0] = blocks[i+1]->track;
                blocks[i]->data[1] = blocks[i+1]->sector;
            }
            remaining -= 120;
        }
        return last;
    }

    FRESULT load(uint8_t& track, uint8_t& sector)
    {
        for(int i=0; i<6; i++) {
            if (!track) {
                return FR_OK;
            }
            if (!blocks[i]) {
                blocks[i] = new CachedBlock(256, track, sector);
            }
            DRESULT dres = fs->prt->read(blocks[i]->data, fs->get_abs_sector(track, sector), 1);
            if (dres != RES_OK) {
                return FR_DISK_ERR;
            }
            // follow chain
            track = blocks[i]->data[0];
            sector = blocks[i]->data[1];
            if (blocks[i]->data[2] != (uint8_t)i) {
                faulty = true;
            }
            record_size = blocks[i]->data[3];

            // full block?
            if (track) {
                datablocks += 120;
            } else if(sector > 16) {
                datablocks += (sector >> 1) - 7;
            }
        }
        return FR_OK;
    }

    FRESULT write(void)
    {
        DRESULT dres;
        for(int i=0;i<6;i++) {
            if (blocks[i]) {
                dres = fs->prt->write(blocks[i]->data, fs->get_abs_sector(blocks[i]->track, blocks[i]->sector), 1);
                if (dres != RES_OK) {
                    return FR_DISK_ERR;
                }
            }
        }
        return FR_OK;
    }

#if SS_DEBUG
    void dump(void)
    {
        printf("*** Side Sector CLUSTER with %d Data Block References ***\n", datablocks);
        for (int i=0;i<6;i++) {
            if(blocks[i]) {
                printf("  ** Sector %d On T:%d/S:%d **\n", i, blocks[i]->track, blocks[i]->sector);
                dump_hex_relative(blocks[i]->data, 64);
            }
        }
    }
#endif
};

class SideSectors
{
    int datablocks;
    uint8_t record_size;
    bool do_super;
    FileSystemCBM *fs;
    CachedBlock *super;
    IndexedList<SideSectorCluster *> clusters;
public:
    SideSectors(FileSystemCBM *fs, uint8_t record_size) : clusters(2, NULL)
    {
        datablocks = 0;
        do_super = (fs->num_sectors > 720);
        this->fs = fs;
        this->record_size = record_size;
        super = NULL;
    }

    ~SideSectors()
    {
        for (int i=0;i<clusters.get_elements();i++) {
            if (clusters[i]) {
                delete clusters[i];
            }
        }
        if (super) {
            delete super;
        }
    }

    bool addDataBlock(int track, int sector)
    {
        // Create Super Side block, if required
        if (do_super && !super) {
            int t=0,s=0;
            if (!fs->get_next_free_sector(t, s)) {
                return false;
            }
            super = new CachedBlock(256, t, s);
            super->data[2] = 0xFE; // super side block marker
        }

        int cluster_index = datablocks / 720;
        int block_in_cluster = datablocks - (cluster_index * 720);

        SideSectorCluster *cluster = clusters[cluster_index];
        if (!cluster) {
            cluster = new SideSectorCluster(fs, record_size);
            clusters.append(cluster);
        }

        if (cluster->addDataBlock(track, sector)) {
            datablocks++;
            return true;
        }
        return false;
    }

    void get_track_sector(int& track, int& sector)
    {
        if (super) {
            track = super->track;
            sector = super->sector;
        } else if (clusters[0]) {
                clusters[0]->get_track_sector(track, sector);
        } else {
            track = 0;
            sector = 0;
        }
    }

    void validate(uint8_t rec_size)
    {
        if(super) {
            int t,s;
            if(clusters[0]) {
                clusters[0]->get_track_sector(t, s);
                super->data[0] = (uint8_t)t;
                super->data[1] = (uint8_t)s;
            }
            CachedBlock *last = NULL;
            for (int i=0;i<clusters.get_elements();i++) {
                if (!clusters[i]) {
                    break;
                }
                clusters[i]->get_track_sector(t, s);
                super->data[3+2*i] = (uint8_t)t;
                super->data[4+2*i] = (uint8_t)s;
                if (last) {
                    last->data[0] = (uint8_t)t;
                    last->data[1] = (uint8_t)s;
                }
                last = clusters[i]->validate(rec_size);
            }
        } else {
            if(clusters[0]) {
                clusters[0]->validate(rec_size);
            }
        }
    }

    int get_number_of_blocks(void)
    {
        int num_blocks = (super) ? 1 : 0;
        num_blocks += (datablocks + 119) / 120;
        return num_blocks;
    }

    FRESULT write(void)
    {
#if SS_DEBUG
        dump();
#endif
        DRESULT dres;
        if(super) {
            dres = fs->prt->write(super->data, fs->get_abs_sector(super->track, super->sector), 1);
            if (dres != RES_OK) {
                return FR_DISK_ERR;
            }
        }
        FRESULT fres = FR_OK;
        for (int i=0;i<clusters.get_elements();i++) {
            if (clusters[i]) {
                fres = clusters[i]->write();
                if (fres != FR_OK) {
                    break;
                }
            }
        }
        return fres;
    }

    FRESULT load(uint8_t track, uint8_t sector)
    {
        FRESULT fres = fs->move_window(fs->get_abs_sector(track, sector));
        if (fres != FR_OK) {
            return fres;
        }
        if (fs->sect_buffer[2] == 0xFE) { // SSS!
            super = new CachedBlock(256, track, sector);
            memcpy(super->data, fs->sect_buffer, 256);
            do_super = true;
            track = fs->sect_buffer[0];
            sector = fs->sect_buffer[1];
        }
        datablocks = 0;
        while(track) {
            SideSectorCluster *cluster = new SideSectorCluster(fs, 0);
            clusters.append(cluster);
            fres = cluster->load(track, sector);
            if (fres != FR_OK)
                return fres;
            datablocks += cluster->get_number_of_data_blocks();
        }
        return fres;
    }

#if SS_DEBUG
    void dump(void)
    {
        printf("\nThis is a dump of the side sector structure, holding %d data block references.\n", datablocks);
        if (super) {
            printf("*** Super Side Sector (T:%d/S:%d):\n", super->track, super->sector);
            dump_hex_relative(super->data, 32);
        }
        for (int i=0;i<clusters.get_elements();i++) {
            clusters[i]->dump();
        }
    }

    void test(int num_blocks)
    {
        for(int i=0;i<num_blocks;i++) {
            int t,s;
            fs->get_track_sector(i, t, s);
            bool ok = addDataBlock(t, s);
        }
        validate(0);
        write();
        int t,s,a;
        get_track_sector(t, s);
        a = fs->get_abs_sector(t, s);
        printf("Side sectors start at %06x\n", 256*a);
    }
#endif
};
