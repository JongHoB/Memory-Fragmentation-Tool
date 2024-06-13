# phymem_fragmenter
- Kernel Loadable Module to fragment Physical Memory

  - Exhausts contiguous physical memory (from a particular order)

  - Makes higher order page allocation request and splits the chunk into individual pages. The KLM fragments the
segment by holding onto the first page and freeing the rest.

  - Currently, this KLM is only allocating to Node 0

- KLMs free the fragmented pages held in custody upon module exit.

# phymem_fragmenter_score_print
- KLM to show current fragmentation score.

---

## Parameter 
- phymem_fragmenter.c
1. `order` : Order of the page to allocate and fragment

   - Common X86 system, `MAX_ORDER` is up to 10. Therefore, you can user 0~10 order number.
3. `fragmentation_score` : Custom Fragmentation score threshold to stop the Fragmenter.

   - This tool is to test `Proactive Compaction` in Linux Kernel. The feature is executed by `Fragmentation Score` of the Node.
   - You can check this first. [Proactive Compaction in Linux Kernel designed by Nitin Gupta
](https://nitingupta.dev/post/proactive-compaction/)

---

### Usage

Ex. `sudo insmod phymem_fragmenter.ko order=9 fragmentation_score=91`
<img width="647" alt="image" src="https://github.com/JongHoB/Memory-Fragmentation-Tool/assets/78012131/98c0939b-54aa-4729-93e9-5aacc734462a">

Ex. `sudo insmod phymem_fragmenter_score_print.ko`

---

### Caution
- Too much fragmentation could cause `Out-Of-Memory Killer`

### Additional Information
- `temp_dir` files are reference files. The code is for userspace program.
