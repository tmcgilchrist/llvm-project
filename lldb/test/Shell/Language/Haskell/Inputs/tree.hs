module Main where

data Tree a = Leaf a | Node (Tree a) (Tree a)

mkTree :: Int -> Tree Int
mkTree 0 = Leaf 42
mkTree n = Node (mkTree (n - 1)) (mkTree (n - 1))

sumTree :: Tree Int -> Int
sumTree (Leaf x) = x
sumTree (Node l r) = sumTree l + sumTree r
{-# NOINLINE sumTree #-}

main :: IO ()
main = do
  let t = mkTree 3
  print (sumTree t)
