;;; this iterates through a control-flow graph to perform constant-propagation analysis. it is adapted from Data Flow Analysis: Theory and Practice by Khedker, Sanyal, and Karkare
;;; author: bt3ze@virginia.edu
(defpackage :pcf2-const
  (:use :common-lisp :pcf2-bc :setmap :utils :pcf2-block-graph :pcf2-flow-utils)
  (:export const-flow-fn
           const-confluence-op
           const-weaker-fn
           )
  )

(in-package :pcf2-const)

;;; this analysis tracks the uses of constants through a program to determine if we can eliminate some gates by propagating constants and to help with other dataflow analyses
;;; A variable x e Var has a constant value c e Const at a program point u if for every path reaching u along which a definition of x reaches u, the value of x is c.

;;; f_n(x) = (x-Kill_n(x) Union Gen_n

;;; Constant propagation is forward data flow problem
;;; we represent the constants available at a program point as a map from variable to constant. if the variable is not a constant, we exclude it from the map (this is a memory consideration)
;;; the confluence operation is defined in terms of applying conf-hat on pairs of the same variable
;; ForAll x1,x2 e L, x1 conf x2 = { <z, dx conf-hat dy > | <z,dx> e x1, <z,dy> e x2, x e Var }
;; our analysis will merge maps of constant values by taking all of the key-value pairs that are common to both maps and all of the key-value pairs for which the keys are unique to one map. Pairs from two maps with the same key but different values will be discarded.
;; Our map-intersect operation uses the same idea, but takes only the key-value pairs that the maps have in common, discarding everything else.

;;; Gen_n(x) = ConstGen_n Union DepGen_n(x)
;;; Kill_n(x) = ConstKill_n Union DepKill_n(x)

;;; in general, Gen and Kill for constant propagation are:
;;;
;;; In_n = { BI                          n is Start
;;;          Meet (p in pred(n)) Out_p   Otw
;;; Out_n = f_n(In_n)
;;; (remember, Out_n is passed to the next block, In_n is an input to this block)

;;; ConstGen_n = { {<x,eval(e,Top)>}  n is assignment x=e, Opd(e) subset Const 
;;;                **{<x,bottom-hat>} n is read(x) ;; read is always alice() or bob()
;;;                /0  otw
;;; DepGen_n(x) = { <x,d>    n is assignment x=e, <x,d> e *x*
;;;                  \0      otw
;;;

;;; ConstKill_n    =  **/0
;;; DepKill_n(x)   = { {<x,d>} n is assignment x=e, <x,d> e *x*
;;;                    {<x,d>} n is read(x), <x,d> e *x*
;;;                     /0                otw
;;; explanation:

;;; val(e,x) = { c if e is c e Const
;;;              d if e is x e Car, <x,d> e *x*
;;;

;;; **because read(x) is a way for us to input non-consts, we move <x,bottom-hat> from ConstGen to ConstKill

;; all unassigned wires are 0, so if something is not found in the consts then it is implicitly 0 (otherwise it will be an integer or pcf2-block-graph:pcf-not-const)

(defparameter input-functions (set-from-list (list "alice" "bob") :comp #'string<))

(defun map-union-without-conflicts (map1 map2)
  (let ((newmap (map-reduce (lambda (map-accum key val)
                              (declare (optimize (debug 3)(speed 0)))
                              #|(if (and (equal key 533)
                                       (not (equal val 1))
                                       (not (equal val 0)))
                                  (break))|#
                              (aif (map-val key map2 t)
                                   (if (equal it val)
                                       map-accum ;; already have the element
                                       (map-insert key 'pcf2-block-graph:pcf-not-const map-accum)) ;; element duplicates not equivalent
                                   (map-insert key val map-accum))) ;; if it's not found, it's new and needs to be added
                            map1
                            map2)))
    ;;(if (equal (map-val 533 map2 t) 3)
      ;;  (break))
     newmap))

(defparameter confluence-operator #'map-union-without-conflicts) ;; this is not set-inter, needs to be updated with a form of map-inter

(defun map-diff (map1 map2)
  ;; map1 without the elements from map2
  (map-reduce (lambda (map key val)
                (declare (ignore val))
                (if (map-val key map t)
                    (map-remove key map)
                    map))
              map2 ;; remove elements from map2
              map1 ;; use map1 as initial
              ))


(defun map-remove-key-set (map set)
  (set-reduce (lambda (newmap key)
                (if (map-find key newmap t)
                    (map-remove key newmap)
                    newmap))
              set
              map))

(defun const-confluence-op (set1 set2)
  (funcall confluence-operator set1 set2))

(defun const-flow-fn (blck cfg use-map)
  ;; this function contains a bit at the end to eliminate extraneous const information we may be carrying around
  (declare (optimize (speed 0) (debug 3)))
  (let ((in-flow (get-out-sets blck cfg #'map-union-without-conflicts)))
    ;;(if (equalp (get-block-id blck) 439) (break))
    (let ((flow (map-union-without-conflicts
                 (map-remove-key-set in-flow (kill (get-block-op blck) blck in-flow))
                 (gen (get-block-op blck) blck in-flow))))
      (if (zerop (mod (get-block-id blck) 100))
          (eliminate-extra-consts flow blck use-map)
      #|(if (equalp (get-block-id blck) 438)
          (break))|#
          flow)))
  )

(defun const-weaker-fn (map1 map2)
  ;; map 1 is weaker than (safely estimates) map 2 if map 1 is a subset of map2
  ;; and every entry in map 1 is either the same as in map 2 or not-const
  ;;(set-subset set1 set2)
;;  (declare (optimize (debug 3)(speed 0)))
  (labels ((weaker-map-vals (m1 m2)
             (map-reduce (lambda (state key m-val1)
                           (declare (optimize (debug 3)(speed 0)))
                           (let ((m-val2 (map-val key m2 t)))
                             #|(if (and (not (equalp m2 (map-empty)))
                                      (not (null (map-val 2 m2 t)))
                                      (not (equalp m-val1 m-val2))
                                      (not (null m-val2))
                                      (not (member key (list 2 3 363 364 365 502 362 265 400 464 465 496 497 498 499))))
                                 (break))|#
                             (and state
                                  (or (equal m-val1 m-val2)
                                      (equalp m-val1 'pcf2-block-graph:pcf-not-const)
                                      (null m-val2)))))
                         m1
                         t)))
    (and
     (weaker-map-vals map1 map2)
     (not (set-subset map2 map1)))
    ))

(defun get-out-sets (blck cfg conf)
  ;;(format t "block preds: ~A~%" (get-block-preds blck))
  (reduce
   (lambda (temp-out pred)
     (let ((pred-out (get-block-consts (get-block-by-id pred cfg))))
       ;;(format t "pred out: ~A~%" pred-out)
       (funcall conf temp-out pred-out)))
   (get-block-preds blck)
   :initial-value (map-empty)
   ;;(get-block-consts blck)
   ))

(defgeneric gen (op blck flow-data)
  (:documentation "this function describes how to compute the gen part of the flow function for each op") 
  )

(defgeneric kill (op blck flow-data)
  (:documentation "this function describes how to compute the kill part of the flow function for each op")
)

(defgeneric const-gen (op blck)
  (:documentation "this function describes how to compute the constant gen part of the flow function for each op")
)

(defgeneric dep-gen (op blck flow-data)
  (:documentation "this function describes how to compute the dependent gen part of the flow function for each op")
)

(defgeneric const-kill (op blck)
  (:documentation "this function describes how to compute the constant kill part of the flow function for each op")
)

(defgeneric dep-kill (op blck flow-data)
  (:documentation "this function describes how to compute the dependent kill part of the flow function for each op")
)

(defmethod gen (op blck flow-data)
  ;; gen = const_gen union dep_gen
  (const-confluence-op (const-gen op (get-block-base blck)) (dep-gen op (get-block-base blck) flow-data)))

(defmethod kill (op blck flow-data)
  ;; kill = const-kill union gep_kill
  ;;(break)
  (const-confluence-op (const-kill op (get-block-base blck)) (dep-kill op (get-block-base blck) flow-data)))
  
;;; macros to define const-gen, dep-gen, const-kill, and dep-kill
(defmacro empty-gen ()
  `(map-empty))

(defmacro empty-kill ()
  `(empty-set))

(defmacro def-const-gen (type &body body)
  `(defmethod const-gen ((op ,type) base)
     (declare (optimize (debug 3) (speed 0)))
     (aif (locally ,@body)
          it
          (empty-gen))))

(defmacro def-dep-gen (type &body body)
  `(defmethod dep-gen ((op ,type) base flow-data)
     (declare (optimize (debug 3) (speed 0)))
     (aif (locally ,@body)
          it
          (empty-gen))))

(defmacro def-const-kill (type &body body)
  `(defmethod const-kill ((op ,type) base)
     (declare (optimize (debug 3) (speed 0)))
     (aif (locally ,@body)
          it
          (empty-kill))))

(defmacro def-dep-kill (type &body body)
  `(defmethod dep-kill ((op ,type) base flow-data)
     (declare (optimize (debug 3) (speed 0)))
     (aif (locally ,@body)
          it
          (empty-kill))))

;; and the macro to write const-gen, dep-gen, const-kill, and dep-kill for each instruction
(defmacro def-gen-kill (type &key (const-gen nil) (dep-gen nil) (const-kill nil) (dep-kill nil))
  `(progn
     (def-const-gen ,type ,const-gen)
     (def-dep-gen ,type ,dep-gen) ; dep-gen always /0 in faint analysis
     (def-const-kill ,type ,const-kill)
     (def-dep-kill ,type ,dep-kill)
  ))

;; gen sets are represented as maps of variable -> value
;; kill sets are represented as sets of variables since their values aren't necessary for the kill


(defmacro with-true-addresses ((&rest syms) &body body)
  `(let ,(loop for sym in syms
            collect `(,sym (+ ,sym (aif base it 0))))
     ,@body))

(defmacro with-true-address (sym &body body)
  `(let ((,sym (+ ,sym base)))
     ,@body))

(defmacro with-true-address-list (lst &body body)
  `(let ((,lst (mapcar (lambda(x) (+ x base)) ,lst)))
     ,@body))


(defmacro with-not-nil-from (a b &body body)
  `(let ((it (if ,a ,a ,b)))
     ,@body ))

(defmacro loginot (a)
  `(if (eq ,a 1) 1 0))

(defmacro singleton-if-found ()
  `(if (map-val dest flow-data t)
       (singleton dest)
       (empty-kill)))


(defun to-n-bit-binary-list (num bits)
  (labels ((to-binary (n depth)
             (if (eq depth 0)
                 (list (mod n 2))
                 (append (list (mod n 2)) (to-binary (floor (/ n 2)) (- depth 1))))))
  (to-binary num (- bits 1))))


(def-gen-kill bits
    :dep-gen (with-slots (dest op1) op
               (with-true-address-list dest
                 (with-true-addresses (op1)
                   (aif (map-extract-val op1 flow-data)
                        (let ((bin-list (to-n-bit-binary-list it (length dest))))
                          (first (reduce (lambda (state bit)
                                           (let ((map (first state))
                                                 (wire (car (second state)))) ;; this is the first from the list of bits
                                             (list (map-insert wire bit map) (cdr (second state))))) ;; cdr second state is the rest of the bits
                                         bin-list ;; reduce over the whole list
                                         :initial-value (list (map-empty) dest))))
                        (error "bits called on non-const")))))
    :dep-kill (with-slots (dest) op
                (with-true-address-list dest
                  (reduce (lambda (set wire)
                            (if (map-val wire flow-data t)
                                (set-insert set wire)
                                set))
                          dest
                          :initial-value (empty-set))))
    )

(def-gen-kill join
    :dep-gen (labels ((all-list-found (map lst)
                        (if (null lst)
                            t
                            (and (not (null (map-extract-val (car lst) map)))
                                 (all-list-found map (cdr lst))))))
               (with-slots (dest op1) op
                 (with-true-address dest
                   (with-true-address-list op1
                     (if (all-list-found flow-data op1)
                         (let ((val (first (reduce 
                                            (lambda (state wire)
                                              (list
                                               (+ (first state)
                                                  (* (map-extract-val wire flow-data)
                                                     (expt 2 (second state))))
                                               (+ (second state) 1)))
                                            op1
                                            :initial-value (list 0 0)))))
#|
                                (loop
                                       for wire in op1
                                       for count from 0 to (- (length op1) 1)
                                       do (format t "~A~%" wire)
                                       with x = (map-extract-val wire flow-data)
                                       summing (* x (expt 2 count)) into dec-var
                                       finally (return dec-var) )))
                           |#
                           (map-singleton dest val))
                         (empty-gen))))))
    :dep-kill (with-slots (dest) op
                (with-true-address dest
                  (if (map-val dest flow-data t)
                      (singleton dest)
                      (empty-kill))))
    )

(defmacro or-defined (op1 op2 data)
  `(or (map-extract-val ,op1 ,data)
       (map-extract-val ,op2 ,data)))

(defmacro and-defined (op1 op2 data)
  `(and (map-extract-val ,op1 ,data)
        (map-extract-val ,op2 ,data)))

(defun flip-bit (o1)
  (if (zerop o1)
      1
      (if (equal o1 1)
          0
          (error "input to gate not 0 or 1"))))

(def-gen-kill gate
    ;; this is where we propagate ANDs with 0, ORs with 1, and NOTs on a const
    ;; we also precompute gate values where we know them beforehand
    :dep-gen (with-slots (dest op1 op2 truth-table) op
               (with-true-addresses (dest op1 op2)
                 (let ((o1 (map-extract-val op1 flow-data))
                       (o2 (map-extract-val op2 flow-data)))
                   (if (or-defined op1 op2 flow-data)
                       (if (and-defined op1 op2 flow-data) ;; if both are constant, we can precompute the gate
                           (progn
                             (assert (or (equal o1 0)(equal o1 1)))
                             (assert (or (equal o2 0)(equal o2 1)))
                             (let ((out-val
                                    (cond
                                      ((equalp truth-table #*0001) (logand o1 o2))
                                      ((equalp truth-table #*1100) (flip-bit o1))
                                      ((equalp truth-table #*0111) (logior o1 o2))
                                      ((equalp truth-table #*0110) (assert (not (equalp op1 op2))) (logxor o1 o2))
                                      ((equalp truth-table #*1001) (flip-bit (logxor o1 o2)))
                                      (t 
                                       (print truth-table)
                                       (error "unknown truth table in gate")))))
                               (map-singleton dest out-val)))
                           (cond
                             ((equalp truth-table #*0001)
                              (if (or (equal o1 0) (equal o2 0))
                                  (map-singleton dest 0)
                                  (cond
                                    ((equal o1 1)
                                     (map-singleton dest (aif o2 it 'pcf2-block-graph:pcf-not-const))) ;; whatever o2 is
                                    ((equal o2 1)
                                     (map-singleton dest (aif o1 it 'pcf2-block-graph:pcf-not-const))) ;; whatever o1 is
                                    (t (map-singleton dest 'pcf2-block-graph:pcf-not-const)))))
                             ((equalp truth-table #*0111)
                              (if (or (equal o1 1)(equal o2 1))
                                  (map-singleton dest 1)
                                  (cond
                                    ((equal 0 o1)
                                     (map-singleton dest (aif o2 it 'pcf2-block-graph:pcf-not-const)))
                                    ((equal 0 o2)
                                     (map-singleton dest (aif o1 it 'pcf2-block-graph:pcf-not-const)))
                                    (t (map-singleton dest 'pcf2-block-graph:pcf-not-const)))))
                             (t (map-singleton dest 'pcf2-block-graph:pcf-not-const))))
                       (map-singleton dest 'pcf2-block-graph:pcf-not-const)))))
    :dep-kill (with-slots (dest) op
                (with-true-address dest
                  (singleton-if-found)))
    )

(def-gen-kill const
    :const-gen (with-slots (dest op1) op
                 (with-true-addresses (dest)
                   ;;(break)
                   (map-singleton dest op1)))
    :dep-kill (with-slots (dest) op
                  (with-true-addresses (dest)
                    (singleton-if-found)))
    )

(def-gen-kill add
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1 op2)
                 (let ((o1 (map-extract-val op1 flow-data))
                       (o2 (map-extract-val op2 flow-data)))
                   (format t "o1: ~A o2: ~A~%" o1 o2) ;; can only add on constants
                   (map-singleton dest (if (and o1 o2) (+ o1 o2) 'pcf2-block-graph:pcf-not-const)))))
    :dep-kill (with-slots (dest) op
                (with-true-address dest
                  (singleton-if-found)))
    )

(def-gen-kill sub
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1 op2)
                 (let ((o1 (map-extract-val op1 flow-data))
                       (o2 (map-extract-val op2 flow-data)))
                   (format t "o1: ~A ot ~A~%" o1 o2) ;; can only add on constants
                   (map-singleton dest (if (and o1 o2) (- o1 o2) 'pcf2-block-graph:pcf-not-const)))))
    :dep-kill (with-slots (dest) op
                (with-true-address dest
                  (singleton-if-found)))
    )

(def-gen-kill mul
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1 op2)
                 (let ((o1 (map-extract-val op1 flow-data))
                       (o2 (map-extract-val op2 flow-data)))
                   (format t "o1: ~A ot ~A~%" o1 o2) ;; can only add on constants
                   (map-singleton dest (if (and o1 o2) (* o1 o2) 'pcf2-block-graph:pcf-not-const)))))
    :dep-kill (with-slots (dest) op
                (with-true-address dest
                  (singleton-if-found)))
    )

(def-gen-kill copy
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1)  
                 (if (equal op2 1)
                     (aif (map-extract-val op1 flow-data)
                          (map-singleton dest it)
                          (map-singleton dest 'pcf2-block-graph:pcf-not-const))
                     (first (reduce (lambda (state oldwire)
                               (let ((map (first state))
                                     (lst (cdr (second state)))
                                     (newwire (car (second state))))
                                 (let ((data (map-val oldwire flow-data t)))
                                   (if data
                                       (list (map-insert newwire data map) lst)
                                       (list (map-insert newwire 'pcf2-block-graph:pcf-not-const map) lst)))))
                               (loop for i from op1 to (+ op1 op2 -1) collect i)
                               :initial-value (list (map-empty) (loop for i from dest to (+ dest op2 -1) collect i)))))))
    :dep-kill (with-slots (dest op2) op
                (with-true-addresses (dest)
                  (if (equal 1 op2)
                      (if (map-val dest flow-data t)
                          (singleton dest)
                          (empty-kill))
                      (reduce (lambda (set var)
                                (let ((data (map-extract-val var flow-data)))
                                  (if data
                                      (set-insert set var)
                                      set)))
                              (loop for i from dest to (+ dest op2 -1) collect i)
                              :initial-value (empty-set)))))
    )


(def-gen-kill mkptr
;;    :const-gen (progn (break)(empty-gen)) ;; no consts
)

(defmacro gen-for-indirection (source-address dest-address length)
  `(if (equal ,length 1)
       (aif (map-extract-val ,source-address flow-data)  ;; it may not always be found; but usually in this case we're copying a condition wire, which usually won't be const (or faint) anyway
            (map-singleton ,dest-address it) 
            (map-singleton ,dest-address  'pcf2-block-graph:pcf-not-const))
       (first (reduce (lambda (state oldwire)
                        (let ((map (first state))
                              (newwire (car (second state))))
                          ;;(break)
                          (aif (map-val oldwire flow-data t)
                               (list (map-insert newwire it map) (cdr (second state)))
                               (list (map-insert newwire 0 map) (cdr (second state))))))
                               ;;(list (map-insert newwire 'pcf2-block-graph:pcf-not-const map) (cdr (second state))))))
                               ;;(error "could not find value of copy wire")))) 
                      (loop for i from ,source-address to (+ ,source-address ,length -1) collect i)
                      :initial-value (list (empty-gen) (loop for i from ,dest-address to (+ ,dest-address ,length -1) collect i))))))

(defmacro kill-for-indirection (source-address dest-address length)
  `(if (equal ,length 1)
       (if (map-find ,dest-address flow-data t) ;; it may not always be found; but usually in this case we're copying a condition wire, which usually won't be const (or faint) anyway
           (singleton ,dest-address)
           (empty-kill))
        (reduce (lambda (set var)
                  (let ((data (map-val var flow-data t)))
                    (if data
                        (set-insert set var)
                        set)))
                (loop for i from ,dest-address to (+ ,dest-address ,length -1) collect i)
                :initial-value (empty-set))))

(def-gen-kill copy-indir
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1)
                 (let ((addr (map-val op1 flow-data)))
                   (gen-for-indirection addr dest op2))))
    :dep-kill (with-slots (dest op1 op2) op
                (with-true-addresses (dest op1)
                  (let ((addr (map-val op1 flow-data)))
                    ;;(break)
                    (kill-for-indirection addr dest op2)))))

(def-gen-kill indir-copy
    :dep-gen (with-slots (dest op1 op2) op
               (with-true-addresses (dest op1)
                 (let ((addr (map-val dest flow-data)))
                   (gen-for-indirection op1 addr op2))))
    :dep-kill (with-slots (dest op1 op2) op
                (with-true-addresses (dest op1)
                  (let ((addr (map-val dest flow-data)))
                    ;;(break)
                         (kill-for-indirection op1 addr op2)))))

(def-gen-kill initbase
    ;;take this opportunity to set wire 0 as pcf2-block-graph:pcf-not-const
    :const-gen (with-slots (base) op
                 (map-insert base 0 ;; the 0th wire in the frame will always point at global condition wire
                             (map-singleton 0 'pcf2-block-graph:pcf-not-const)))
    )

(def-gen-kill call
    :const-gen (with-slots (newbase fname) op
                 (with-true-address newbase
                   (if (set-member fname input-functions)
                       (reduce (lambda (map x)
                                 (map-insert x 'pcf2-block-graph:pcf-not-const map))
                               (loop for i from newbase to (+ 32 newbase -1) collect i)
                               :initial-value (map-empty))
                       (empty-gen))))
    :dep-kill (with-slots (newbase fname) op
                (with-true-address newbase
                  (set-from-list
                   (loop for i from newbase to (+ 32 newbase -1) collect i))))
    )

(def-gen-kill branch
    :const-gen (with-slots (cnd) op
                 (with-true-address cnd
                   (map-singleton cnd 'pcf2-block-graph:pcf-not-const))))

(def-gen-kill ret) ;; no consts
(def-gen-kill label) ;; no consts -- might have to set base
(def-gen-kill clear) ;; no consts -- yet. when interprocedural analysis is added, this will be important.
