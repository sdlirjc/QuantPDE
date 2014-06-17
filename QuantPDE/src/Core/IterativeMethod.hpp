#ifndef QUANT_PDE_CORE_ITERATIVE_METHOD
#define QUANT_PDE_CORE_ITERATIVE_METHOD

#include <array>        // std::array
#include <cstdlib>      // size_t
#include <forward_list> // std::forward_list
#include <functional>   // std::functional
#include <memory>       // std::unique_ptr
#include <tuple>        // std::tuple
#include <utility>      // std::forward
#include <vector>       // std::vector

// TODO: Documentation

namespace QuantPDE {

const int DEFAULT_STEPPER_LOOKBACK = 6;
const int DEFAULT_TOLERANCE_ITERATION_LOOKBACK = 2;

/**
 * A pure virtual class representing a solver for equations of type \f$Ax=b\f$.
 */
class LinearSolver {

protected:

	Matrix A;

	virtual void initialize() = 0;

public:

	/**
	 * Constructor.
	 */
	LinearSolver() noexcept {
	}

	/**
	 * Destructor.
	 */
	virtual ~LinearSolver() {
	}

	// Disable copy constructor and assignment operator.
	LinearSolver(const LinearSolver &) = delete;
	LinearSolver &operator=(const LinearSolver &) & = delete;

	/**
	 * Initializes the linear solver with a matrix. If solving a linear
	 * system with a constant left-hand-side multiple times, this call
	 * should occur only once.
	 * @param A The left-hand-side matrix.
	 */
	template <typename M>
	void initialize(M &&A) {
		this->A = std::forward<M>(A);
		initialize();
	}

	/**
	 * Solves the linear system. This should only be called after a call
	 * to initialize.
	 * @param b The right-hand-side.
	 * @param guess An initial guess (ignored for noniterative methods).
	 * @return The solution.
	 * @see QuantPDE::LinearSolver::initialize
	 */
	virtual Vector solve(const Vector &b, const Vector &guess) const = 0;

};

/**
 * Solves \f$Ax=b\f$ with BiCGSTAB.
 */
class BiCGSTABSolver : public LinearSolver {

	BiCGSTAB solver;

	virtual void initialize() {
		solver.compute(A);
		assert( solver.info() == Eigen::Success );
	}

public:

	/**
	 * Constructor.
	 */
	BiCGSTABSolver() noexcept : LinearSolver() {
	}

	virtual Vector solve(const Vector &b, const Vector &guess) const {
		Vector v = solver.solveWithGuess(b, guess);
		assert( solver.info() == Eigen::Success );
		return v;
	}

};

////////////////////////////////////////////////////////////////////////////////

/**
 * Keeps track of the n most recent iterands in an iterative method.
 */
class IterandHistory final {

	template <typename T, size_t Index>
	class TupleSelector final {

		const IterandHistory *history;

	public:

		template <typename H>
		TupleSelector(H &history) noexcept : history(&history) {
		}

		// Disable copy constructor and assignment operator
		TupleSelector(const TupleSelector &) = delete;
		TupleSelector &operator=(const TupleSelector &) = delete;

		T operator[](int index) const {
			assert(index >  -history->lookback);
			assert(index <=  history->lookback);

			int position = (history->tail - 1 + history->lookback
					+ index) % history->lookback;

			assert(position < history->size);

			return std::get<Index>( history->data[position] );
		}

	};

public:

	typedef TupleSelector<Real, 0> Times;
	typedef TupleSelector<const Vector &, 1> Iterands;

private:

	typedef std::tuple<Real, Vector> T;
	T *data;

	Times ts;
	Iterands its;

	size_t tail;
	int lookback;

	#ifndef NDEBUG
	size_t size;
	#endif

public:

	/**
	 * Constructor.
	 * @param lookback How many iterands to keep track of.
	 */
	IterandHistory(int lookback) noexcept : ts(*this), its(*this),
			lookback(lookback) {
		assert(lookback > 0);
		data = new T[lookback];
		clear();
	}

	/**
	 * Destructor.
	 */
	virtual ~IterandHistory() {
		delete [] data;
	}

	// Disable copy constructor and assignment operator.
	IterandHistory(const IterandHistory &) = delete;
	IterandHistory &operator=(const IterandHistory &) = delete;

	/**
	 * Removes all stored iterands.
	 */
	void clear() {
		tail = 0;

		#ifndef NDEBUG
		size = 0;
		#endif
	}

	/**
	 * Adds an iterand. Automatically removes the oldest iterand if too many
	 * iterands are being sotred.
	 * @param element The iterand.
	 */
	template <typename V>
	void push(Real time, V &&iterand) {
		data[tail] = std::make_tuple(time, std::forward<V>(iterand));
		tail = (tail + 1) % lookback;

		#ifndef NDEBUG
		if(size < lookback) {
			size++;
		}
		#endif
	}

	const Times &times() const {
		return ts;
	}

	const Iterands &iterands() const {
		return its;
	}

};

class Iteration;

class LinearSystem {

public:

	/**
	 * Constructor.
	 */
	LinearSystem() noexcept {
	}

	/**
	 * Destructor.
	 */
	virtual ~LinearSystem() {
	}

	// Disable copy constructor and assignment operator.
	LinearSystem(const LinearSystem &) = delete;
	LinearSystem &operator=(const LinearSystem &) = delete;

	/**
	 * @return False if and only if the left-hand-side matrix (A) has
	 *         changed since the previous call to A.
	 * @see QuantPDE::LinearSystem::A
	 */
	virtual bool isATheSame() const {
		// Default: assume A changes
		return false;
	}

	/**
	 * @return The left-hand-side matrix (A).
	 */
	virtual Matrix A(Real time) = 0;

	/**
	 * @return The right-hand-side vector (b).
	 */
	virtual Vector b(Real time) = 0;

};

////////////////////////////////////////////////////////////////////////////////

// TODO: Copy/move/assignment

template <Index Dimension>
class WrapperFunction final {

	static_assert(Dimension > 0, "Dimension must be positive");

	class Base {

	public:

		Base() noexcept {
		}

		virtual ~Base() {
		}

		// Disable copy constructor and assignment operator.
		Base(const Base &) = delete;
		Base &operator=(const Base &) = delete;

		virtual Real value(std::array<Real, Dimension + 1> coordinates)
				const = 0;

		virtual bool isConstantInTime() const {
			// Default: not constant w.r.t. time
			return false;
		}

		virtual bool isControllable() const {
			// Default: not controllable
			return false;
		}

		virtual void setInput(const Vector &input) {
			// Default: do nothing
		}

		virtual void setInput(Vector &&input) {
			// Default: do nothing
		}

		virtual std::unique_ptr<Base> clone() const = 0;

	};

	class Constant final : public Base {

		Real constant;

	public:

		Constant(Real constant) noexcept : Base(), constant(constant) {
		}

		virtual Real value(std::array<Real, Dimension + 1> coordinates)
				const {
			return constant;
		}

		virtual bool isConstantInTime() const {
			return true;
		}

		virtual std::unique_ptr<Base> clone() const {
			return std::unique_ptr<Base>(new Constant(constant));
		}

	};

	class FunctionOfSpaceAndTime final : public Base {

		Function<Dimension + 1> function;

	public:

		template <typename F>
		FunctionOfSpaceAndTime(F &&function) noexcept : Base(),
				function(function) {
		}

		virtual Real value(std::array<Real, Dimension + 1> coordinates)
				const {
			return packAndCall<Dimension + 1>(function,
					coordinates.data());
		}

		virtual std::unique_ptr<Base> clone() const {
			return std::unique_ptr<Base>(
					new FunctionOfSpaceAndTime(function));
		}

	};

	class FunctionOfSpace final : public Base {

		Function<Dimension> function;

	public:

		template <typename F>
		FunctionOfSpace(F &&function) noexcept : Base(),
				function(function) {
		}

		virtual Real value(std::array<Real, Dimension + 1> coordinates)
				const {
			return packAndCall<Dimension>(function,
					coordinates.data() + 1);
		}

		virtual bool isConstantInTime() const {
			return true;
		}

		virtual std::unique_ptr<Base> clone() const {
			return std::unique_ptr<Base>(
					new FunctionOfSpace(function));
		}

	};

	class Control final : public Base {

		std::unique_ptr<InterpolantFactoryBase<Dimension>> factory;
		std::unique_ptr<Interpolant<Dimension>> interpolant;

	public:

		Control(
			std::unique_ptr<InterpolantFactoryBase<Dimension>> factory,
			std::unique_ptr<Interpolant<Dimension>> interpolant = nullptr
		) noexcept :
			factory( std::move(factory) ),
			interpolant( std::move(interpolant) ) {
		}

		virtual Real value(std::array<Real, Dimension + 1> coordinates)
				const {
			assert(interpolant != nullptr);

			// Pack and call
			NaryMethodConst<Real, Interpolant<Dimension>,
					Dimension, Real> tmp =
					&Interpolant<Dimension>::operator();
			return packAndCall<Dimension>(*interpolant, tmp,
					coordinates.data() + 1);
		}

		virtual bool isControllable() const {
			return true;
		}

		virtual void setInput(const Vector &input) {
			interpolant = std::move( factory->interpolant(input) );
		}

		virtual void setInput(Vector &&input) {
			interpolant = std::move( factory->interpolant(
					std::move(input) ) );
		}

		virtual std::unique_ptr<Base> clone() const {
			return std::unique_ptr<Base>(
				new Control(
					factory->clone(),
					interpolant->clone()
				)
			);
		}

	};

	std::unique_ptr<Base> base;

public:

	WrapperFunction(Real constant) noexcept {
		base = std::unique_ptr<Base>(new Constant(constant));
	}

	WrapperFunction(const Function<Dimension + 1> &function) noexcept {
		base = std::unique_ptr<Base>(
			new FunctionOfSpaceAndTime(
				function
			)
		);
	}

	WrapperFunction(Function<Dimension + 1> &&function) noexcept {
		base = std::unique_ptr<Base>(
			new FunctionOfSpaceAndTime(
				std::move(function)
			)
		);
	}

	WrapperFunction(const Function<Dimension> &function) noexcept {
		base = std::unique_ptr<Base>(
			new FunctionOfSpace(
				function
			)
		);
	}

	WrapperFunction(Function<Dimension> &&function) noexcept {
		base = std::unique_ptr<Base>(
			new FunctionOfSpace(
				std::move(function)
			)
		);
	}

	WrapperFunction(std::unique_ptr<InterpolantFactoryBase<Dimension>>
			factory) noexcept {
		base = std::unique_ptr<Base>(
			new Control(
				std::move(factory)
			)
		);
	}

	WrapperFunction(const WrapperFunction &that) noexcept
			: base(that.base->clone()) {
	}

	WrapperFunction(WrapperFunction &&that) noexcept
			: base( std::move(that.base) ) {
	}

	WrapperFunction &operator=(const WrapperFunction &that) & noexcept {
		base = that.base->clone();
	}

	WrapperFunction &operator=(WrapperFunction &&that) & noexcept {
		base = std::move(that.base);
	}

	template <typename ...Ts>
	Real operator()(Ts ...coordinates) const {
		return base->value( {{coordinates...}} );
	}

	bool isConstantInTime() const {
		return base->isConstantInTime();
	}

	bool isControllable() const {
		return base->isControllable();
	}

	void setInput(const Vector &input) {
		base->setInput(input);
	}

	template <template <Index> class T = PiecewiseLinear, typename D>
	static WrapperFunction make(D &domain) {
		return WrapperFunction(
			std::unique_ptr<InterpolantFactoryBase<Dimension>>(
				new typename T<Dimension>::Factory(domain)
			)
		);
	}

};

typedef WrapperFunction<1> WrapperFunction1;
typedef WrapperFunction<2> WrapperFunction2;
typedef WrapperFunction<3> WrapperFunction3;

template <Index Dimension>
using Control = WrapperFunction<Dimension>;

typedef Control<1> Control1;
typedef Control<2> Control2;
typedef Control<3> Control3;

////////////////////////////////////////////////////////////////////////////////

class ControlledLinearSystem : public LinearSystem {

	virtual void setInputs(Vector *inputs) = 0;

	/**
	 * @return The number of controls.
	 */
	virtual size_t controlDimension() const = 0;

public:

	ControlledLinearSystem() noexcept : LinearSystem() {
	}

	template <typename ...Ts>
	void setInputs(Ts &&...inputs) {
		assert(controlDimension() == sizeof...(Ts));

		Vector in[] { std::forward<Ts>(inputs)... };
		setInputs(in);
	}

	template <typename ...Ts>
	void setInputsx(Ts ...inputs) {
		Vector in[] {inputs...};
		setInputs(in);
	}

};

template <Index Dimension>
class SimpleControlledLinearSystem : public ControlledLinearSystem {

	std::vector<WrapperFunction<Dimension> *> controls;

	virtual void setInputs(Vector *inputs) {
		for(auto control : controls) {
			control->setInput( std::move(*(inputs++)) );
		}
	}

	virtual size_t controlDimension() const {
		return controls.size();
	}

protected:

	void registerControl(WrapperFunction<Dimension> &wrapper) {
		if(wrapper.isControllable()) {
			controls.push_back(&wrapper);
		}
	}

public:

	template <typename ...Ts>
	SimpleControlledLinearSystem() noexcept : ControlledLinearSystem() {
	}

};

typedef SimpleControlledLinearSystem<1> SimpleControlledLinearSystem1;
typedef SimpleControlledLinearSystem<2> SimpleControlledLinearSystem2;
typedef SimpleControlledLinearSystem<3> SimpleControlledLinearSystem3;

////////////////////////////////////////////////////////////////////////////////

/**
 * Used to generate the left and right-hand sides of the linear system at each
 * iteration.
 */
class LinearSystemIteration : public LinearSystem {

	Iteration *iteration;

	/**
	 * Method called before iteration begins.
	 */
	virtual void clear() {
		// Default: do nothing
	}

	/**
	 * Method called on the start of an iteration.
	 */
	virtual void onIterationStart() {
		// Default: do nothing
	}

	/**
	 * Method called at the end of an iteration.
	 */
	virtual void onIterationEnd() {
		// Default: do nothing
	}

protected:

	/**
	 * @return The times associated with the most recent iterands.
	 */
	const IterandHistory::Times &times() const;

	/**
	 * @return The most recent iterands.
	 */
	const IterandHistory::Iterands &iterands() const;

	/**
	 * @return The time with which the next solution is associated with.
	 */
	Real nextTime() const;

	/**
	 * @return False if and only if the timestep size was different on the
	 *         previous iteration.
	 */
	bool isTimestepTheSame() const {
		Real t2 = nextTime();
		Real t1 = times()[0];
		Real t0 = times()[-1];
		return (t2 - t1) == (t1 - t0);
	}

public:

	/**
	 * Constructor.
	 */
	LinearSystemIteration() noexcept : LinearSystem(), iteration(nullptr) {
	}

	/**
	 * @return The left-hand-side matrix (A).
	 */
	virtual Matrix A() = 0;

	/**
	 * @return The right-hand-side vector (b).
	 */
	virtual Vector b() = 0;

	virtual Matrix A(Real) {
		return A();
	}

	virtual Vector b(Real) {
		return b();
	}

	// TODO: Document
	void setIteration(Iteration &iteration);

	friend Iteration;

};

/**
 * Executes an iterative method.
 */
class Iteration {

	Iteration *child;

	// Nonownership
	std::forward_list<LinearSystemIteration *> systems;

	IterandHistory history;
	Real implicitTime;

	std::vector<size_t> its;

	void clearIterations() {
		its.clear();
		if(child) {
			child->clearIterations();
		}
	}

	/**
	 * Method called before iteration begins.
	 */
	virtual void clear() {
		// Default: do nothing
	}

	/**
	 * @return A step taken in time (positive or negative). Zero if none is
	 *         taken.
	 */
	virtual Real timestep() {
		return 0.;
	}

	/**
	 * @return The initial time associated with this iteration.
	 */
	virtual Real initialTime(Real time) const {
		return time;
	}

	/**
	 * @return True if and only if this iteration is done.
	 */
	virtual bool done() const = 0;

	Vector iterateUntilDone(
		const Vector &initialIterand,
		LinearSystemIteration &root,
		LinearSolver &solver,
		Real time,
		bool initialized
	) {
		its.push_back(0);

		clear();

		for(auto system : systems) {
			system->clear();
		}

		time = initialTime(time);

		// Keep track of the initial iterand
		history.clear();
		history.push(time, initialIterand);

		// Iterate until done
		implicitTime = time;

		// Use macros to prevent branching inside iteration loop

		// Important that time is advanced before onIterationStart()
		// calls
		#define QUANT_PDE_TMP_HEAD                                     \
				do {                                           \
					implicitTime += timestep();            \
					for(auto system : systems) {           \
						system->onIterationStart();    \
					}                                      \
				} while(0)

		// Must be initialized after the first iteration
		#define QUANT_PDE_TMP_TAIL                                     \
				do {                                           \
					initialized = true;                    \
					its.back()++;                          \
					for(auto system : systems) {           \
						system->onIterationEnd();      \
					}                                      \
				} while(0)

		// Iterating at least once allows us to make assumptions
		// about how many iterands we have available to us for
		// processing when implementing the done() method

		if(child) {

			// Inductive case (perform inner iteration)

			do {
				QUANT_PDE_TMP_HEAD;

				// Add a new iterand
				history.push(
					implicitTime,
					child->iterateUntilDone(
						iterands()[0],
						root,
						solver,
						times()[0],
						initialized
					)
				);

				QUANT_PDE_TMP_TAIL;
			} while( !done() );

		} else {

			// Base case (solve linear system)

			do {
				QUANT_PDE_TMP_HEAD;

				// Only compute A if necessary
				if(!initialized || !root.isATheSame()) {
					solver.initialize(root.A());
				}

				history.push(
					implicitTime,
					solver.solve(
						root.b(),
						iterands()[0]
					)
				);

				QUANT_PDE_TMP_TAIL;
			} while( !done() );

		}

		#undef QUANT_PDE_TMP_HEAD
		#undef QUANT_PDE_TMP_TAIL

		return iterands()[0];
	}

protected:

	/**
	 * @return The times associated with the most recent iterands.
	 */
	const IterandHistory::Times &times() const {
		return history.times();
	}

	/**
	 * @return The most recent iterands.
	 */
	const IterandHistory::Iterands &iterands() const {
		return history.iterands();
	}

public:

	/**
	 * Constructor.
	 */
	Iteration(int lookback) noexcept : child(nullptr), history(lookback) {
		assert(lookback >= 2); // Guarantee at least two iterands are
		                       // saved
	}

	virtual ~Iteration() {
	}

	// TODO: Document
	void setInnerIteration(Iteration &innerIteration) {
		child = &innerIteration;
	}

	// Disable copy constructor and assignment operator.
	Iteration(const Iteration &) = delete;
	Iteration &operator=(const Iteration &) = delete;

	// TODO: Document
	Vector iterateUntilDone(
		const Vector &initialIterand,
		LinearSystemIteration &root,
		LinearSolver &solver
	) {
		clearIterations();

		return iterateUntilDone(
			initialIterand,
			root,
			solver,
			0.,
			false
		);
	}

	/**
	 * @return Vector with number of iterations.
	 */
	const std::vector<size_t> &iterations() const {
		return its;
	}

	/**
	 * @return The average number of iterations.
	 */
	Real meanIterations() const {
		Real mean = its[0];
		for(size_t i = 1; i < its.size(); i++) {
			mean = (i * mean + its[i]) / (i + 1);
		}
		return mean;
	}

	friend LinearSystemIteration;
};

void LinearSystemIteration::setIteration(Iteration &iteration) {
	if(this->iteration) {
		this->iteration->systems.remove(this);
	}

	this->iteration = &iteration;
	iteration.systems.push_front(this);
}

const IterandHistory::Iterands &LinearSystemIteration::iterands() const {
	return iteration->history.iterands();
}

const IterandHistory::Times &LinearSystemIteration::times() const {
	return iteration->history.times();
}

Real LinearSystemIteration::nextTime() const {
	return iteration->implicitTime;
}

////////////////////////////////////////////////////////////////////////////////

template <bool Forward>
class ConstantStepper : public Iteration {

	Real startTime, endTime, dt;
	unsigned n, steps;

	virtual void clear() {
		n = 0;
	}

	virtual Real initialTime(Real) const {
		return Forward ? startTime : endTime; // TODO: Optimize
	}

	virtual Real timestep() {
		n++;
		return Forward ? dt : -dt; // TODO: Optimize
	}

	virtual bool done() const {
		return n >= steps;
	}

public:

	ConstantStepper(
		Real startTime,
		Real endTime,
		unsigned steps,
		int lookback = DEFAULT_STEPPER_LOOKBACK
	) noexcept :
		Iteration(lookback),
		startTime(startTime),
		endTime(endTime),
		dt( (endTime - startTime) / steps ),
		steps(steps)
	{
		assert(startTime >= 0.);
		assert(startTime < endTime);
		assert(steps > 0);
	}

};

typedef ConstantStepper<false> ReverseConstantStepper;
typedef ConstantStepper<true > ForwardConstantStepper;

////////////////////////////////////////////////////////////////////////////////

template <bool Forward>
class VariableStepper : public Iteration {

	Real startTime, endTime, dt, target, scale, time;
	Real (VariableStepper<Forward>::*_step)();

	Real _step0() {
		_step = &VariableStepper::_step1;
		return Forward ? dt : -dt; // TODO: Optimize
	}

	Real _step1() {
		const Vector
			&v1 = iterands()[ 0],
			&v0 = iterands()[-1]
		;

		// Tested 2014-06-08
		dt *= target / ( v1 - v0 ).cwiseAbs().cwiseQuotient(
			( scale * Vector::Ones( v1.size() ) ).cwiseMax(
				v1.cwiseAbs().cwiseMax(
					v0.cwiseAbs()
				)
			)
		).maxCoeff();

		// TODO: Optimize
		if(Forward) {
			if(time + dt > endTime) {
				dt = endTime - time;
				time = endTime;
			} else {
				time += dt;
			}

			return dt;
		} else {
			if(time - dt < startTime) {
				dt = time - startTime;
				time = startTime;
			} else {
				time -= dt;
			}

			return -dt;
		}
	}

	virtual void clear() {
		_step = &VariableStepper::_step0;
		time = Forward ? startTime : endTime; // TODO: Optimize
	}

	virtual Real initialTime(Real) const {
		return endTime;
	}

	virtual Real timestep() {
		return (this->*_step)();
	}

	virtual bool done() const {
		if(Forward) {
			return time <= startTime;
		} else {
			return time >= endTime;
		}
	}

public:

	VariableStepper(
		Real startTime,
		Real endTime,
		Real dt,
		Real target,
		Real scale = 1,
		int lookback = DEFAULT_STEPPER_LOOKBACK
	) noexcept :
		Iteration(lookback),
		startTime(startTime),
		endTime(endTime),
		dt(dt),
		target(target),
		scale(scale)
	{
		assert(startTime >= 0.);
		assert(startTime < endTime);
		assert(dt > 0);
		assert(target > 0);
		assert(scale > 0);
		assert(lookback >= 2); // Need at least two iterands to
		                       // variable step
	}

};

typedef VariableStepper<false> ReverseVariableStepper;
typedef VariableStepper<true > ForwardVariableStepper;

////////////////////////////////////////////////////////////////////////////////

class ToleranceIteration : public Iteration {

	Real tolerance;
	Real scale;

	virtual bool done() const {
		const Vector
			&v1 = iterands()[ 0],
			&v0 = iterands()[-1]
		;

		// Tested 2014-06-07
		return
			( v1 - v0 ).cwiseAbs().cwiseQuotient(
				( scale * Vector::Ones(v1.size()) ).cwiseMax(
					v1.cwiseAbs()
				)
			).maxCoeff() < tolerance;
	}

public:

	ToleranceIteration(Real tolerance = 1e-6, Real scale = 1,
			int lookback = DEFAULT_TOLERANCE_ITERATION_LOOKBACK)
			noexcept : Iteration(lookback), tolerance(tolerance),
			scale(scale) {
		assert(tolerance > 0);
		assert(scale > 0);
	}

};

} // QuantPDE

#endif

