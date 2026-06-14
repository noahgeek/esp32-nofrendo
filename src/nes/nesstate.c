/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** nesstate.c
**
** state saving/loading
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../noftypes.h"
#include "nesstate.h"
#include "../gui.h"
#include "nes.h"
#include "../log.h"
#include "../osd.h"
#include "../libsnss/libsnss.h"
#include "../cpu/nes6502.h"

#ifdef ESP_PLATFORM
#include "esp_partition.h"
#include "esp_heap_caps.h"
#define STATE_PART_LABEL "coredump"
#define STATE_SLOT_SIZE  (32 * 1024)   /* 每个槽位 32KB（coredump 分区 64KB 容纳 2 个槽位） */
#define STATE_MAGIC      "NES2"
#endif

#define FIRST_STATE_SLOT 0
#define LAST_STATE_SLOT 9

static int state_slot = FIRST_STATE_SLOT;

/* Set the state-save slot to use (0 - 9) */
void state_setslot(int slot)
{
   /* Don't send a message if we're already at that slot */
   if (state_slot != slot && slot >= FIRST_STATE_SLOT && slot <= LAST_STATE_SLOT)
   {
      state_slot = slot;
      gui_sendmsg(GUI_WHITE, "State slot set to %d", slot);
   }
}

static int save_baseblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;

   ASSERT(state);

   nes6502_getcontext(state->cpu);
   ppu_getcontext(state->ppu);

   snssFile->baseBlock.regA = state->cpu->a_reg;
   snssFile->baseBlock.regX = state->cpu->x_reg;
   snssFile->baseBlock.regY = state->cpu->y_reg;
   snssFile->baseBlock.regFlags = state->cpu->p_reg;
   snssFile->baseBlock.regStack = state->cpu->s_reg;
   snssFile->baseBlock.regPc = state->cpu->pc_reg;

   snssFile->baseBlock.reg2000 = state->ppu->ctrl0;
   snssFile->baseBlock.reg2001 = state->ppu->ctrl1;

   memcpy(snssFile->baseBlock.cpuRam, state->cpu->mem_page[0], 0x800);
   memcpy(snssFile->baseBlock.spriteRam, state->ppu->oam, 0x100);
   memcpy(snssFile->baseBlock.ppuRam, state->ppu->nametab, 0x1000);

   /* Mask off priority color bits */
   for (i = 0; i < 32; i++)
      snssFile->baseBlock.palette[i] = state->ppu->palette[i] & 0x3F;

   snssFile->baseBlock.mirrorState[0] = (state->ppu->page[8] + 0x2000 - state->ppu->nametab) / 0x400;
   snssFile->baseBlock.mirrorState[1] = (state->ppu->page[9] + 0x2400 - state->ppu->nametab) / 0x400;
   snssFile->baseBlock.mirrorState[2] = (state->ppu->page[10] + 0x2800 - state->ppu->nametab) / 0x400;
   snssFile->baseBlock.mirrorState[3] = (state->ppu->page[11] + 0x2C00 - state->ppu->nametab) / 0x400;

   snssFile->baseBlock.vramAddress = state->ppu->vaddr;
   snssFile->baseBlock.spriteRamAddress = state->ppu->oam_addr;
   snssFile->baseBlock.tileXOffset = state->ppu->tile_xofs;

   return 0;
}

static int save_vramblock(nes_t *state, SNSS_FILE *snssFile)
{
   ASSERT(state);

   if (NULL == state->rominfo->vram)
      return -1;

   if (state->rominfo->vram_banks > 2)
      return -1;

   snssFile->vramBlock.vramSize = VRAM_8K * state->rominfo->vram_banks;
   memcpy(snssFile->vramBlock.vram, state->rominfo->vram, snssFile->vramBlock.vramSize);
   return 0;
}

static int save_sramblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;
   bool written = false;
   int sram_length;

   ASSERT(state);

   sram_length = state->rominfo->sram_banks * SRAM_1K;

   for (i = 0; i < sram_length; i++)
   {
      if (state->rominfo->sram[i])
      {
         written = true;
         break;
      }
   }

   if (false == written) return -1;
   if (state->rominfo->sram_banks > 8) return -1;

   snssFile->sramBlock.sramSize = SRAM_1K * state->rominfo->sram_banks;
   snssFile->sramBlock.sramEnabled = true;
   memcpy(snssFile->sramBlock.sram, state->rominfo->sram, snssFile->sramBlock.sramSize);

   return 0;
}

static int save_soundblock(nes_t *state, SNSS_FILE *snssFile)
{
   ASSERT(state);

   apu_getcontext(state->apu);

   snssFile->soundBlock.soundRegisters[0x00] = state->apu->rectangle[0].regs[0];
   snssFile->soundBlock.soundRegisters[0x01] = state->apu->rectangle[0].regs[1];
   snssFile->soundBlock.soundRegisters[0x02] = state->apu->rectangle[0].regs[2];
   snssFile->soundBlock.soundRegisters[0x03] = state->apu->rectangle[0].regs[3];
   snssFile->soundBlock.soundRegisters[0x04] = state->apu->rectangle[1].regs[0];
   snssFile->soundBlock.soundRegisters[0x05] = state->apu->rectangle[1].regs[1];
   snssFile->soundBlock.soundRegisters[0x06] = state->apu->rectangle[1].regs[2];
   snssFile->soundBlock.soundRegisters[0x07] = state->apu->rectangle[1].regs[3];
   snssFile->soundBlock.soundRegisters[0x08] = state->apu->triangle.regs[0];
   snssFile->soundBlock.soundRegisters[0x0A] = state->apu->triangle.regs[1];
   snssFile->soundBlock.soundRegisters[0x0B] = state->apu->triangle.regs[2];
   snssFile->soundBlock.soundRegisters[0X0C] = state->apu->noise.regs[0];
   snssFile->soundBlock.soundRegisters[0X0E] = state->apu->noise.regs[1];
   snssFile->soundBlock.soundRegisters[0x0F] = state->apu->noise.regs[2];
   snssFile->soundBlock.soundRegisters[0x10] = state->apu->dmc.regs[0];
   snssFile->soundBlock.soundRegisters[0x11] = state->apu->dmc.regs[1];
   snssFile->soundBlock.soundRegisters[0x12] = state->apu->dmc.regs[2];
   snssFile->soundBlock.soundRegisters[0x13] = state->apu->dmc.regs[3];
   snssFile->soundBlock.soundRegisters[0x15] = state->apu->enable_reg;

   return 0;
}

static int save_mapperblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;
   ASSERT(state);

   mmc_getcontext(state->mmc);

   if (0 == state->mmc->intf->number) return -1;

   nes6502_getcontext(state->cpu);

   for (i = 0; i < 4; i++)
      snssFile->mapperBlock.prgPages[i] = (state->cpu->mem_page[(i + 4) * 2] - state->rominfo->rom) >> 13;

   if (state->rominfo->vrom_banks)
   {
      for (i = 0; i < 8; i++)
         snssFile->mapperBlock.chrPages[i] = (ppu_getpage(i) - state->rominfo->vrom + (i * 0x400)) >> 10;
   }
   else
   {
      for (i = 0; i < 8; i++)
         snssFile->mapperBlock.chrPages[i] = i;
   }

   if (state->mmc->intf->get_state)
      state->mmc->intf->get_state(&snssFile->mapperBlock);

   return 0;
}

static void load_baseblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;

   ASSERT(state);

   nes6502_getcontext(state->cpu);
   ppu_getcontext(state->ppu);

   state->cpu->a_reg = snssFile->baseBlock.regA;
   state->cpu->x_reg = snssFile->baseBlock.regX;
   state->cpu->y_reg = snssFile->baseBlock.regY;
   state->cpu->p_reg = snssFile->baseBlock.regFlags;
   state->cpu->s_reg = snssFile->baseBlock.regStack;
   state->cpu->pc_reg = snssFile->baseBlock.regPc;

   state->ppu->ctrl0 = snssFile->baseBlock.reg2000;
   state->ppu->ctrl1 = snssFile->baseBlock.reg2001;

   memcpy(state->cpu->mem_page[0], snssFile->baseBlock.cpuRam, 0x800);
   memcpy(state->ppu->oam, snssFile->baseBlock.spriteRam, 0x100);
   memcpy(state->ppu->nametab, snssFile->baseBlock.ppuRam, 0x1000);
   memcpy(state->ppu->palette, snssFile->baseBlock.palette, 0x20);

   for (i = 0; i < 8; i++)
      state->ppu->palette[i << 2] = state->ppu->palette[0] | 0x80;

   for (i = 0; i < 4; i++)
   {
      state->ppu->page[i + 8] = state->ppu->page[i + 12] =
          state->ppu->nametab + (snssFile->baseBlock.mirrorState[i] * 0x400) - (0x2000 + (i * 0x400));
   }

   state->ppu->vaddr = snssFile->baseBlock.vramAddress;
   state->ppu->oam_addr = snssFile->baseBlock.spriteRamAddress;
   state->ppu->tile_xofs = snssFile->baseBlock.tileXOffset;

   state->ppu->flipflop = 0;
   state->ppu->strikeflag = false;

   nes6502_setcontext(state->cpu);
   ppu_setcontext(state->ppu);

   ppu_write(PPU_CTRL0, state->ppu->ctrl0);
   ppu_write(PPU_CTRL1, state->ppu->ctrl1);
   ppu_write(PPU_VADDR, (uint8)(state->ppu->vaddr >> 8));
   ppu_write(PPU_VADDR, (uint8)(state->ppu->vaddr & 0xFF));
}

static void load_vramblock(nes_t *state, SNSS_FILE *snssFile)
{
   ASSERT(state);
   if (state->rominfo->vram && snssFile->vramBlock.vramSize <= VRAM_8K)
      memcpy(state->rominfo->vram, snssFile->vramBlock.vram, snssFile->vramBlock.vramSize);
}

static void load_sramblock(nes_t *state, SNSS_FILE *snssFile)
{
   ASSERT(state);
   if (state->rominfo->sram && snssFile->sramBlock.sramSize <= SRAM_8K)
      memcpy(state->rominfo->sram, snssFile->sramBlock.sram, snssFile->sramBlock.sramSize);
}

static void load_soundblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;
   ASSERT(state);
   for (i = 0; i < 0x15; i++)
   {
      if (i != 0x13)
         apu_write(0x4000 + i, snssFile->soundBlock.soundRegisters[i]);
   }
}

static void load_mapperblock(nes_t *state, SNSS_FILE *snssFile)
{
   int i;

   ASSERT(state);

   mmc_getcontext(state->mmc);

   for (i = 0; i < 4; i++)
      mmc_bankrom(8, 0x8000 + (i * 0x2000), snssFile->mapperBlock.prgPages[i]);

   if (state->rominfo->vrom_banks)
   {
      for (i = 0; i < 8; i++)
         mmc_bankvrom(1, i * 0x400, snssFile->mapperBlock.chrPages[i]);
   }
   else if (state->rominfo->vram)
   {
      for (i = 0; i < 8; i++)
         ppu_setpage(1, i, state->rominfo->vram);
   }

   if (state->mmc->intf->set_state)
      state->mmc->intf->set_state(&snssFile->mapperBlock);

   mmc_setcontext(state->mmc);
}

#ifdef ESP_PLATFORM
/* ===== ESP32 partition-based save/load =====
 * 直接把 SNSS 子结构体（不含 fp）作为一个紧凑二进制 blob 存进 coredump 分区。
 * 布局：
 *   [magic:4] [flags:4] [baseBlock] [vramBlock] [sramBlock] [mapperBlock] [soundBlock]
 * flags bit0: vramBlock 有效；bit1: sramBlock 有效；bit2: mapperBlock 有效
 * 大小：约 0x1931 + 0x4002 + 0x2003 + 0x98 + 0x16 = ~24KB，能装进 32KB 槽位。
 */

typedef struct {
   char magic[4];
   uint32_t flags;
   SnssBaseBlock   baseBlock;
   SnssVramBlock   vramBlock;
   SnssSramBlock   sramBlock;
   SnssMapperBlock mapperBlock;
   SnssSoundBlock  soundBlock;
} state_blob_t;

#define STATE_FLAG_VRAM   0x1
#define STATE_FLAG_SRAM   0x2
#define STATE_FLAG_MAPPER 0x4
#endif /* ESP_PLATFORM */

int state_save(void)
{
   nes_t *machine;
   machine = nes_getcontextptr();
   ASSERT(machine);

#ifdef ESP_PLATFORM
   /* 限制最多 2 个槽位（coredump 分区 64KB / 32KB per slot） */
   int slot = state_slot;
   if (slot > 1) slot = 1;

   const esp_partition_t *part = esp_partition_find_first(
       ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, STATE_PART_LABEL);
   if (!part) {
      gui_sendmsg(GUI_RED, "Save failed: no '%s' partition", STATE_PART_LABEL);
      return -1;
   }

   /* state_blob_t 约 24KB，太大不能放栈，用堆 */
   state_blob_t *blob = (state_blob_t *)heap_caps_malloc(sizeof(state_blob_t),
                                                         MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
   if (!blob) {
      gui_sendmsg(GUI_RED, "Save failed: no memory");
      return -1;
   }
   memset(blob, 0, sizeof(*blob));
   memcpy(blob->magic, STATE_MAGIC, 4);

   /* SNSS_FILE 结构里有大数组（vram 16KB），栈放不下，用堆 */
   SNSS_FILE *sf = (SNSS_FILE *)heap_caps_malloc(sizeof(SNSS_FILE),
                                                 MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
   if (!sf) {
      heap_caps_free(blob);
      gui_sendmsg(GUI_RED, "Save failed: no memory (sf)");
      return -1;
   }
   memset(sf, 0, sizeof(*sf));

   if (save_baseblock(machine, sf) == 0) {
      memcpy(&blob->baseBlock, &sf->baseBlock, sizeof(SnssBaseBlock));
   }
   if (save_vramblock(machine, sf) == 0) {
      memcpy(&blob->vramBlock, &sf->vramBlock, sizeof(SnssVramBlock));
      blob->flags |= STATE_FLAG_VRAM;
   }
   if (save_sramblock(machine, sf) == 0) {
      memcpy(&blob->sramBlock, &sf->sramBlock, sizeof(SnssSramBlock));
      blob->flags |= STATE_FLAG_SRAM;
   }
   if (save_soundblock(machine, sf) == 0) {
      memcpy(&blob->soundBlock, &sf->soundBlock, sizeof(SnssSoundBlock));
   }
   if (save_mapperblock(machine, sf) == 0) {
      memcpy(&blob->mapperBlock, &sf->mapperBlock, sizeof(SnssMapperBlock));
      blob->flags |= STATE_FLAG_MAPPER;
   }

   heap_caps_free(sf);

   /* 写入分区：先擦后写 */
   esp_err_t err = esp_partition_erase_range(part, slot * STATE_SLOT_SIZE, STATE_SLOT_SIZE);
   if (err != ESP_OK) {
      heap_caps_free(blob);
      gui_sendmsg(GUI_RED, "Save erase failed (%d)", err);
      return -1;
   }
   err = esp_partition_write(part, slot * STATE_SLOT_SIZE, blob, sizeof(*blob));
   heap_caps_free(blob);
   if (err != ESP_OK) {
      gui_sendmsg(GUI_RED, "Save write failed (%d)", err);
      return -1;
   }

   gui_sendmsg(GUI_GREEN, "State %d saved", state_slot);
   return 0;
#else
   return -1;
#endif
}

int state_load(void)
{
   nes_t *machine;
   machine = nes_getcontextptr();
   ASSERT(machine);

#ifdef ESP_PLATFORM
   int slot = state_slot;
   if (slot > 1) slot = 1;

   const esp_partition_t *part = esp_partition_find_first(
       ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, STATE_PART_LABEL);
   if (!part) {
      gui_sendmsg(GUI_RED, "Load failed: no '%s' partition", STATE_PART_LABEL);
      return -1;
   }

   state_blob_t *blob = (state_blob_t *)heap_caps_malloc(sizeof(state_blob_t),
                                                         MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
   if (!blob) {
      gui_sendmsg(GUI_RED, "Load failed: no memory");
      return -1;
   }

   esp_err_t err = esp_partition_read(part, slot * STATE_SLOT_SIZE, blob, sizeof(*blob));
   if (err != ESP_OK || memcmp(blob->magic, STATE_MAGIC, 4) != 0) {
      heap_caps_free(blob);
      gui_sendmsg(GUI_RED, "State %d not found", state_slot);
      return -1;
   }

   /* 用原版 load_* 函数恢复（SNSS_FILE 也用堆分配） */
   SNSS_FILE *sf = (SNSS_FILE *)heap_caps_malloc(sizeof(SNSS_FILE),
                                                 MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
   if (!sf) {
      heap_caps_free(blob);
      gui_sendmsg(GUI_RED, "Load failed: no memory (sf)");
      return -1;
   }
   memset(sf, 0, sizeof(*sf));
   memcpy(&sf->baseBlock,   &blob->baseBlock,   sizeof(SnssBaseBlock));
   memcpy(&sf->vramBlock,   &blob->vramBlock,   sizeof(SnssVramBlock));
   memcpy(&sf->sramBlock,   &blob->sramBlock,   sizeof(SnssSramBlock));
   memcpy(&sf->mapperBlock, &blob->mapperBlock, sizeof(SnssMapperBlock));
   memcpy(&sf->soundBlock,  &blob->soundBlock,  sizeof(SnssSoundBlock));

   load_baseblock(machine, sf);

   if (blob->flags & STATE_FLAG_VRAM)
      load_vramblock(machine, sf);
   if (blob->flags & STATE_FLAG_SRAM)
      load_sramblock(machine, sf);
   if (blob->flags & STATE_FLAG_MAPPER)
      load_mapperblock(machine, sf);

   load_soundblock(machine, sf);

   heap_caps_free(sf);
   heap_caps_free(blob);
   gui_sendmsg(GUI_GREEN, "State %d restored", state_slot);
   return 0;
#else
   return -1;
#endif
}
