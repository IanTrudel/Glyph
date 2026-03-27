parent(tom, bob). parent(tom, liz).
parent(bob, ann). parent(bob, pat).
ancestor(X, Y) :- parent(X, Y).
ancestor(X, Y) :- parent(X, Z), ancestor(Z, Y).
:- write(descendants_of_tom), nl, fail.
:- ancestor(tom, X), write(X), nl, fail.
:- write(done), nl.
