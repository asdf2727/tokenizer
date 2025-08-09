# Automated Tokenization as an Optimization Problem

Do you wish that you could have more trouble with preprocessing text for LLMs? Are you using BPE or WordPiece and it works just too fast and well?

Don't worry! I have devised a new method that is a lot slower, finickier and almost as good as the other ones! Instead of a simple greedy algorithm, I turn to simulated annealing to decide which possible tokens to include or exclude in my final solution!

## How to use

Download [this](https://www.kaggle.com/datasets/ltcmdrdata/plain-text-wikipedia-202011?resource=download-directory) dataset and modify the link in the main file (it's a WIP) to reference the folder with the extracted files. The program will then parse all files and create a hidden metadata and candidates file that will be used for training. Finally, a tokens file will be created and ran on the last file as a benchmark. After this, you can input any text to see how the tokenizer handles it.

## Note
The parameters for annealing are chosen with vibes, but they should work pretty well for this particular data set (I later plan to make an adaptive cooling schedule).