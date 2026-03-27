member(X, [X|_]).
member(X, [_|T]) :- member(X, T).
append([], L, L).
append([H|T], L, [H|R]) :- append(T, L, R).
length([], 0).
length([_|T], N) :- length(T, N1), N is N1 + 1.
reverse([], []).
reverse([H|T], R) :- reverse(T, RT), append(RT, [H], R).
:- member(X, [a,b,c]), write(X), nl, fail.
:- append([1,2],[3,4],L), write(L), nl.
:- reverse([a,b,c],R), write(R), nl.
