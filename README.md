A simple C++ implementation of `prof2-ipol` [Professor2](https://gitlab.com/hepcedar/professor)
==
Simply because I want every instance of `prof2-ipol` to be handling a limited subset of bins rather than all of them at once.

Tools:
 - `scan`: Scans all TH1D Objects in given ROOT file
 - `main`: Do interpolation in similar way as `prof2-ipol` in Professor2 but
   - Auto order considers minimization of $\sum (p-O)^2$
   - By default only handles a reduced set of bins (to scale on different nodes)
   - Bin error not handled.
