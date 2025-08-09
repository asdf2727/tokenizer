# Automated Tokenization as an Optimization Problem

Do you wish that you could have more trouble with preprocessing text for LLMs? Are you using BPE or WordPiece and it works just too fast and well?

Don't worry! I have devised a new method that is a lot slower, finickier and almost as good as the other ones! Instead of a simple greedy algorithm, I turn to simulated annealing to decide which possible tokens to include or exclude in my final solution!

## How to use

1. Clone the repo
2. Download [this](https://www.kaggle.com/datasets/ltcmdrdata/plain-text-wikipedia-202011?resource=download-directory) dataset
3. modify the path to the data folder in the main file (it's a WIP).
4. In the cloned repo, run

   ```shell
   cmake -B cmake-build && cmake --build "cmake-build" --target tokenizer
   ```

5. Run the `cmake-build/tokenizer` executable. If using a relative path in step 3, remember to run from the same directory used there as reference.

Once the `.tokens.json` file is built in the data folder, you can comment out `#define RUN_SIM` in main to skip generation of a new vocabulary.

## Note
The parameters for annealing (somewhere in `tokenizer/TokenGenerator.cpp`) are chosen with vibes, but they should work pretty well for this particular data set (I plan to make an adaptive cooling schedule later).