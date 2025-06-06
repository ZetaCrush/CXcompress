# GPU compression algorithm
In each thread, check random number generator seed and for each thread find random word in dictionary for each word in text. Random words should not match word at given position, so each thread will narrow the scope of possible words that match the given word. Narrowing is also done by limiting random word choices to those that haven't followed the previous word previously in the text

For instances where the random choices line up with the actual words, special markers can be added to handle these "right guess" that are actually wrong
