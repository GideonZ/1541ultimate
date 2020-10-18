#include "filesystem_d64.h"

class SideSectorCluster
{
    int datablocks;
    uint8_t record_size;
    CachedBlock *blocks[6];
    FileSystemCBM *fs;
    uint8_t start_track;
    uint8_t start_sector;
public:
    SideSectorCluster(FileSystemCBM *fs, uint8_t recordSize) {
        start_track = 0;
        start_sector = 0;
        datablocks = 0;
        record_size = recordSize;
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

    CachedBlock *validate(void)
    {
        uint8_t local_references[12];
        memset(local_references, 0, 12);
        for(int i=0;i<6;i++) {
            if (blocks[i]) {
                local_references[0+2*i] = blocks[i]->track;
                local_references[1+2*i] = blocks[i]->sector;
            }
        }
        CachedBlock *last = NULL;
        for(int i=0;i<6;i++) {
            if (blocks[i]) {
                last = blocks[i];
                memcpy(blocks[i]->data + 4, local_references, 12);
            }
            if ((i != 5) && (blocks[i+1])) {
                blocks[i]->data[0] = blocks[i+1]->track;
                blocks[i]->data[1] = blocks[i+1]->sector;
            }
        }
        return last;
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

        return cluster->addDataBlock(track, sector);
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

    void validate(void)
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
                last = clusters[i]->validate();
            }
        } else {
            if(clusters[0]) {
                clusters[0]->validate();
            }
        }
    }

    FRESULT write(void)
    {
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

    void test(void)
    {
        for(int i=0;i<400;i++) {
            int t,s;
            fs->get_track_sector(i, t, s);
            bool ok = addDataBlock(t, s);
        }
        validate();
        write();
        int t,s,a;
        get_track_sector(t, s);
        a = fs->get_abs_sector(t, s);
        printf("Side sectors start at %06x\n", 256*a);
    }

};
